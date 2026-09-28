"""Re-import generated FBXs and report object separation and Blender metre bounds."""
import bpy, os
from mathutils import Vector

ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),"..",".."))
OUT=os.path.join(ROOT,"Content","DigitalTwin","Art","R16HD")
names=["Body_Actual","DriveSteerWheel_Actual","LoadWheel_Actual","Underframe_Actual","MastOuter_Actual","MastInner_Actual","MastLift_Actual","ReachCarriage_Actual","ForkCarriage_Actual","ForkLeft_Actual","ForkRight_Actual","Body_UE_Normalized","MastOuter_UE_Normalized","MastInner_UE_Normalized","MastLift_UE_Normalized","ForkLeft_UE_Normalized","ForkRight_UE_Normalized"]
lines=["# R16 HD FBX re-import verification", "", "All bounds are Blender metres after importing the FBXs Blender exported.", "", "| FBX | Mesh objects | Bounds X x Y x Z (m) |", "| --- | ---: | --- |"]
for name in names:
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=os.path.join(OUT,name+".fbx"))
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    pts=[]
    for o in meshes:
        pts += [o.matrix_world @ Vector(c) for c in o.bound_box]
    lo=Vector((min(p.x for p in pts),min(p.y for p in pts),min(p.z for p in pts))); hi=Vector((max(p.x for p in pts),max(p.y for p in pts),max(p.z for p in pts)))
    d=hi-lo
    lines.append(f"| `{name}.fbx` | {len(meshes)} | {d.x:.4f} x {d.y:.4f} x {d.z:.4f} |")
lines += ["", "## Master assembly wheel / ground check", "", "| Wheel object | Diameter x width (m) | Lowest world Z (m) |", "| --- | --- | --- |"]
bpy.ops.wm.open_mainfile(filepath=os.path.join(OUT,"R16HD_AutomatedForklift.blend"))
def bounds(obj):
    pts=[obj.matrix_world @ Vector(c) for c in obj.bound_box]
    lo=Vector((min(p.x for p in pts),min(p.y for p in pts),min(p.z for p in pts))); hi=Vector((max(p.x for p in pts),max(p.y for p in pts),max(p.z for p in pts)))
    return hi-lo,lo
wheel_tires=[o for o in bpy.data.objects if o.type=='MESH' and (o.name.startswith("DriveWheel_Tire") or o.name.startswith("LoadWheelLeft_Tire") or o.name.startswith("LoadWheelRight_Tire"))]
for o in sorted(wheel_tires,key=lambda item:item.name):
    d,lo=bounds(o); lines.append(f"| `{o.name}` | {d.z:.4f} x {d.y:.4f} | {lo.z:.4f} |")
lines += ["", "| Frame check | Lowest world Z (m) |", "| --- | ---: |"]
for name in ("Underframe_MastLowerSill","Underframe_CenterUnderframe"):
    o=bpy.data.objects.get(name)
    if o: lines.append(f"| `{name}` | {bounds(o)[1].z:.4f} |")
report=os.path.join(OUT,"FBX_Reimport_Verification.md")
open(report,"w",encoding="utf-8").write("\n".join(lines)+"\n")
print("\n".join(lines))
