"""Generate the R 16 HD automated config. 4 visual proxy and FBX replacements.

Blender units are metres. Vehicle coordinates: +X forward (fork direction), +Y left,
+Z up. Actual FBXs preserve metric dimensions and component pivots. Normalized FBXs
are 1 m primitive-space equivalents for the current C++ component scaling.
"""
import bpy, os, math
from mathutils import Vector

# Start from an empty scene; Blender's default Cube must never appear in asset previews.
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "Content", "DigitalTwin", "Art", "R16HD")
PREVIEW = os.path.join(OUT, "Previews")
os.makedirs(OUT, exist_ok=True); os.makedirs(PREVIEW, exist_ok=True)

# Source dimensions converted to metres. Wheel dimensions and ground clearances are confirmed from PDF page 2.
L, BODY_W, MAX_W, BODY_H = 2.659, 1.290, 1.570, .950
WHEELBASE = 1.453
DRIVE_R, DRIVE_W = .180, .130  # 1x drive/steer wheel: 360 mm x 130 mm, polyurethane
LOAD_R, LOAD_W = .1425, .100   # 2x load wheels: 285 mm x 100 mm, polyurethane
CLEARANCE_MAST, CLEARANCE_CENTER = .040, .044
MAST_H, MAST_W = 5.000, .900                       # TEMP max lift / mast width
FORK_L, FORK_W, FORK_T = 1.150, .100, .045

def mat(name, color, metallic=0.0, rough=.45):
    m = bpy.data.materials.new(name); m.diffuse_color = (*color, 1)
    m.use_nodes = True; p = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    p.inputs["Base Color"].default_value = (*color, 1); p.inputs["Metallic"].default_value = metallic; p.inputs["Roughness"].default_value = rough
    return m
RED=mat("R16 red", (.62,.018,.012), .25,.3); BLACK=mat("Charcoal",(.018,.02,.022),.35,.3)
POLYURETHANE=mat("Polyurethane tire",(.045,.052,.058),.05,.48); STEEL=mat("Fork steel",(.19,.21,.23),.82,.28); DARK=mat("Mast steel",(.055,.065,.075),.78,.34); WHITE=mat("Safety decal",(.82,.82,.78),.1,.35)

def coll(name):
    c=bpy.data.collections.new(name); bpy.context.scene.collection.children.link(c); return c
def link(obj,c):
    for q in list(obj.users_collection): q.objects.unlink(obj)
    c.objects.link(obj); return obj
def box(c,n,loc,scale,material,bev=.0):
    bpy.ops.mesh.primitive_cube_add(location=loc); o=bpy.context.object; o.name=n; o.dimensions=scale; bpy.ops.object.transform_apply(location=False,rotation=False,scale=True); link(o,c)
    if material:o.data.materials.append(material)
    if bev:
        mod=o.modifiers.new("Soft production edges","BEVEL"); mod.width=bev; mod.segments=3
    return o
def cyl(c,n,loc,rad,depth,material,rot=(math.pi/2,0,0),bevel=.012):
    bpy.ops.mesh.primitive_cylinder_add(vertices=32,radius=rad,depth=depth,location=loc,rotation=rot); o=bpy.context.object; o.name=n; link(o,c); o.data.materials.append(material)
    if bevel: bevel_mod=o.modifiers.new("Tire edge","BEVEL"); bevel_mod.width=bevel; bevel_mod.segments=2
    return o
def beam(c,n,loc,dims,material=DARK): return box(c,n,loc,dims,material,.008)
def export(c,name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in c.objects: o.select_set(True)
    bpy.context.view_layer.objects.active=next(iter(c.objects))
    # Unreal's current Interchange importer keeps Blender's metre vertex values when
    # FBX_SCALE_UNITS is used.  Disable unit scaling so the FBX exporter emits the
    # centimetre conversion once; UE then imports the real metric dimensions in cm.
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT,name+".fbx"),use_selection=True,object_types={'MESH'},apply_unit_scale=False,apply_scale_options='FBX_SCALE_NONE',global_scale=1.0,axis_forward='-Z',axis_up='Y',add_leaf_bones=False,mesh_smooth_type='FACE')

def body_collection():
    c=coll("Body_Actual")
    # Pivot is the BodyVisual/component centre. Lower red battery body, black operator canopy and counterweight.
    box(c,"Body_RedLower",(-.10,0,-.16),(2.34,BODY_W,.55),RED,.045)
    box(c,"Body_RedNose",(1.12,0,-.20),(.42,1.08,.42),RED,.07)
    box(c,"Counterweight_Black",(-1.12,0,.02),(.42,1.20,.72),BLACK,.08)
    box(c,"OperatorPod_Black",(-.35,0,.30),(1.00,.93,.55),BLACK,.035)
    box(c,"OperatorPod_RedAccent",(-.28,0,.52),(.78,.98,.12),RED,.02)
    box(c,"ControlConsole",(.02,-.30,.57),(.42,.22,.22),BLACK,.015)
    box(c,"Seat",(-.48,0,.62),(.42,.52,.16),BLACK,.03)
    box(c,"SeatBack",(-.67,.0,.82),(.12,.52,.48),BLACK,.025)
    # overhead guard / visible product silhouette
    for y in (-.43,.43):
        beam(c,"GuardPost",(-.55,y,.92),(.055,.055,.92),BLACK)
    beam(c,"GuardRoof",(-.55,0,1.36),(1.02,.98,.07),BLACK)
    beam(c,"GuardCrossbar",(-.10,0,1.12),(.06,.98,.06),BLACK)
    # lamps / product label
    cyl(c,"Beacon",(-.96,0,1.43),.045,.06,WHITE,rot=(0,0,0))
    # Keep the outermost body envelope at the specified 2.659 m overall length.
    box(c,"BrandStripe",(-1.319,-.607,.05),(.02,.006,.25),WHITE,.001)
    return c
def wheel_collection(name, radius, width):
    # Every object uses wheel-centre origin. Wheel housings are deliberately in Underframe_Actual, not here.
    c=coll(name); cyl(c,"Tire",(0,0,0),radius,width,POLYURETHANE,bevel=0)
    cyl(c,"WheelHub",(0,-width*.51,0),radius*.47,.008,STEEL,bevel=.002)
    cyl(c,"WheelCap",(0,-width*.56,0),radius*.17,.008,BLACK,bevel=.002)
    return c
def underframe_collection():
    """BodyCollision-child lower frame: distinct from forks and has 40/44 mm ground clearances."""
    c=coll("Underframe_Actual")
    # BodyVisual pivot is 0.475 m above ground in assembly preview; lowest frame surfaces are 0.040/0.044 m above ground.
    beam(c,"CenterUnderframe",(-.05,0,-.391),(2.05,.68,.08),DARK) # lower face: 44 mm ground clearance
    beam(c,"MastLowerSill",(1.18,0,-.395),(.32,.90,.08),DARK)     # lower face: 40 mm under mast
    # Fixed support legs are structural outriggers; they are separate from both forks and fork carriage.
    beam(c,"LeftSupportLeg",(.48,.57,-.365),(1.42,.20,.14),RED)
    beam(c,"RightSupportLeg",(.48,-.57,-.365),(1.42,.20,.14),RED)
    beam(c,"LeftLegTopRail",(.48,.57,-.255),(1.30,.16,.10),BLACK)
    beam(c,"RightLegTopRail",(.48,-.57,-.255),(1.30,.16,.10),BLACK)
    # Rear central drive module and front load-wheel covers are frame members, never part of rotating wheel FBXs.
    box(c,"DriveWheelHousing",(-.72,0,-.255),(.56,.38,.34),BLACK,.035)
    box(c,"DriveWheelOpening",(-.72,0,-.375),(.38,.31,.13),DARK,.02)
    for y,label in ((.64,"Left"),(-.64,"Right")):
        box(c,label+"LoadWheelCover",(.733,y,-.245),(.48,.24,.25),BLACK,.028)
        beam(c,label+"WheelGuard",(.733,y,-.115),(.52,.28,.06),DARK)
    return c
def mast_collection(name,depth,width,height):
    c=coll(name)
    # two channel uprights, transverse braces, all centred around the C++ visual slot pivot
    for y in (-width*.42,width*.42): beam(c,"Upright",(0,y,0),(depth,.075,height),DARK)
    for z in (-height*.42,0,height*.42): beam(c,"CrossMember",(0,0,z),(depth,width,.055),DARK)
    return c
def reach_collection():
    c=coll("ReachCarriage_Actual")
    beam(c,"ReachRailLeft",(0,-.31,0),(.72,.08,.12),DARK); beam(c,"ReachRailRight",(0,.31,0),(.72,.08,.12),DARK)
    beam(c,"ReachCross",(0,0,.07),(.12,.72,.09),DARK); cyl(c,"HydraulicCylinder",(-.12,0,-.10),.045,.55,STEEL,rot=(0,math.pi/2,0)); return c
def carriage_collection():
    c=coll("ForkCarriage_Actual")
    beam(c,"CarriageFrame",(0,0,.22),(.12,.82,.44),DARK); beam(c,"UpperBar",(.08,0,.40),(.16,.95,.07),STEEL); beam(c,"LowerBar",(.08,0,.08),(.16,.95,.07),STEEL)
    for y in (-.30,.30): cyl(c,"SideShiftRoller",(.13,y,.30),.055,.05,STEEL,rot=(math.pi/2,0,0))
    return c
def fork_collection(name):
    c=coll(name)
    # Slot pivot is the centre of the horizontal tine, matching current visual component locations.
    box(c,"ForkTine",(0,0,0),(FORK_L,FORK_W,FORK_T),STEEL,.006)
    box(c,"ForkHeel",(-FORK_L*.46,0,.22),(FORK_T,FORK_W,.46),STEEL,.006)
    return c

body=body_collection(); drive=wheel_collection("DriveSteerWheel_Actual",DRIVE_R,DRIVE_W); load=wheel_collection("LoadWheel_Actual",LOAD_R,LOAD_W); underframe=underframe_collection()
outer=mast_collection("MastOuter_Actual",.12,MAST_W,MAST_H); inner=mast_collection("MastInner_Actual",.08,.70,MAST_H*.90); lift=mast_collection("MastLift_Actual",.05,.55,MAST_H*.76)
reach=reach_collection(); carriage=carriage_collection(); left=fork_collection("ForkLeft_Actual"); right=fork_collection("ForkRight_Actual")
for c in (body,drive,load,underframe,outer,inner,lift,reach,carriage,left,right): export(c,c.name)

# Slot-normalised mesh set: current C++ scale uses a 1m cube baseline; retain detailed source in blend + actual FBXs.
for source, dims in ((body,(L,BODY_W,BODY_H)),(outer,(.12,MAST_W,MAST_H)),(inner,(.08,.70,MAST_H*.9)),(lift,(.05,.55,MAST_H*.76)),(left,(FORK_L,FORK_W,FORK_T)),(right,(FORK_L,FORK_W,FORK_T))):
    c=coll(source.name.replace("Actual","UE_Normalized"));
    for o in source.objects:
        d=o.copy(); d.data=o.data.copy(); c.objects.link(d); d.location=Vector((d.location.x/dims[0],d.location.y/dims[1],d.location.z/dims[2])); d.scale=Vector((d.scale.x/dims[0],d.scale.y/dims[1],d.scale.z/dims[2]))
    export(c,c.name)

# These are FBX export sources, not the assembled vehicle. In particular the normalized
# fork heel is intentionally tall in unit-cube space, so hide all source collections by default.
for c in (body,drive,load,underframe,outer,inner,lift,reach,carriage,left,right):
    c.hide_viewport = True
    c.hide_render = True
for c in bpy.data.collections:
    if c.name.endswith("_UE_Normalized"):
        c.hide_viewport = True
        c.hide_render = True

# Assembly preview: linked duplicates preserve source collections while applying current C++ hierarchy placements.
assembly=coll("R16HD_AssemblyPreview")
def instance(source,name,loc):
    for o in source.objects:
        d=o.copy(); d.data=o.data.copy(); d.name=name+"_"+o.name; d.location += Vector(loc); assembly.objects.link(d)
instance(body,"Body",(0,0,.475))
instance(underframe,"Underframe",(0,0,.475))
# PDF layout: one rear-centre steer/drive wheel, then two front load wheels exactly 1.453 m ahead.
drive_x=-.720; load_x=drive_x+WHEELBASE
instance(drive,"DriveWheel",(drive_x,0,DRIVE_R))
instance(load,"LoadWheelLeft",(load_x,.64,LOAD_R))
instance(load,"LoadWheelRight",(load_x,-.64,LOAD_R))
mast_x=L*.5-.10; instance(outer,"MastOuter",(mast_x,0,MAST_H*.5)); instance(inner,"MastInner",(mast_x-.08,0,MAST_H*.45)); instance(lift,"MastLift",(mast_x-.14,0,MAST_H*.38))
instance(reach,"Reach",(mast_x,0,.35)); instance(carriage,"Carriage",(mast_x+.20,0,.10)); instance(left,"ForkLeft",(mast_x+.20+FORK_L*.5,-.30,.04)); instance(right,"ForkRight",(mast_x+.20+FORK_L*.5,.30,.04))

# Ground, lights and camera are preview-only helpers: separate collection and never FBX-exported.
preview_helpers = coll("Preview_Environment")
bpy.ops.mesh.primitive_plane_add(size=30, location=(0,0,0)); ground=bpy.context.object; ground.name="PreviewGround"; ground.data.materials.append(mat("Ground",(.055,.06,.065),0,.8))
link(ground,preview_helpers)
bpy.ops.object.light_add(type='AREA', location=(3,-4,6)); key=bpy.context.object; key.name="PreviewKeyLight"; key.data.energy=1200; key.data.shape='DISK'; key.data.size=5; link(key,preview_helpers)
bpy.ops.object.light_add(type='AREA', location=(-4,3,3)); fill=bpy.context.object; fill.name="PreviewFillLight"; fill.data.energy=900; fill.data.size=4; link(fill,preview_helpers)
bpy.ops.object.camera_add(); cam=bpy.context.object; cam.name="PreviewCamera"; link(cam,preview_helpers); bpy.context.scene.camera=cam
def aim(obj,pt): obj.rotation_euler=(Vector(pt)-obj.location).to_track_quat('-Z','Y').to_euler()
scene=bpy.context.scene; scene.render.engine='BLENDER_EEVEE'; scene.render.resolution_x=1200; scene.render.resolution_y=800; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'; scene.world.color=(.035,.035,.04)
for name,loc in (("front",(6,-.1,3.0)),("side",(0,-7,1.7)),("three_quarter",(5,-6,2.4)),("underside",(2.8,-4.5,-1.2))):
    ground.hide_render = (name == "underside")
    cam.location=loc; aim(cam,(.35,0,1.25)); scene.render.filepath=os.path.join(PREVIEW,"R16HD_"+name+".png"); bpy.ops.render.render(write_still=True)

# Preserve all source collections + actual model in a readable master file.
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,"R16HD_AutomatedForklift.blend"))
print("R16 HD assets generated:", OUT)
