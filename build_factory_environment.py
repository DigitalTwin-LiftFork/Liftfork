import unreal, math

assets = unreal.EditorAssetLibrary
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
target = '/Game/DigitalTwin/Factory'
if assets.does_asset_exist(target):
    raise RuntimeError('Factory already exists; refusing to overwrite')
if not assets.duplicate_asset('/Game/DigitalTwin/Map1', target):
    raise RuntimeError('Cannot duplicate Map1')
level.load_level(target)
forks = [a for a in actors.get_all_level_actors() if 'AutomatedForklift' in a.get_class().get_name()]
origin = forks[0].get_actor_location() if forks else unreal.Vector(0,0,0)
# GameMode creates the forklift at PlayerStart on Play. Avoid serializing the
# existing sensor's class-default private render-target import data into a map.
rotation = forks[0].get_actor_rotation() if forks else unreal.Rotator()
for old in actors.get_all_level_actors():
    if isinstance(old, unreal.PlayerStart):
        actors.destroy_actor(old)
for forklift in forks:
    actors.destroy_actor(forklift)
start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(origin.x,origin.y,0),rotation)
start.set_actor_label('Factory_ForkliftStart')
# Keep the existing forklift at its authored position. Factory floor stays at Z=0.
ox, oy = origin.x, origin.y
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
palette = {'Concrete':(.28,.31,.33), 'Wall':(.68,.71,.69), 'Steel':(.08,.13,.18),
           'RackBlue':(.025,.16,.34), 'SafetyOrange':(.95,.25,.025),
           'Yellow':(1,.68,.025), 'Walkway':(.04,.28,.17), 'Wood':(.42,.25,.11),
           'Machine':(.22,.38,.35), 'Black':(.025,.03,.035), 'White':(.9,.92,.9)}
mats = {}
for name,color in palette.items():
    path='/Game/DigitalTwin/FactoryAssets/M_'+name
    mat=assets.load_asset(path) if assets.does_asset_exist(path) else None
    if not mat:
        mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_'+name,'/Game/DigitalTwin/FactoryAssets',unreal.Material,unreal.MaterialFactoryNew())
        expr=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
        expr.set_editor_property('constant',unreal.LinearColor(*color,1))
        unreal.MaterialEditingLibrary.connect_material_property(expr,'',unreal.MaterialProperty.MP_BASE_COLOR)
        rough=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant)
        rough.set_editor_property('r',.75)
        unreal.MaterialEditingLibrary.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        assets.save_loaded_asset(mat)
    mats[name]=mat
count=0
def box(name,pos,size,material,collision=True):
    global count
    a=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(ox+pos[0],oy+pos[1],pos[2]))
    a.set_actor_label('Factory_'+name)
    a.set_folder_path('Factory/'+name.split('_')[0])
    c=a.static_mesh_component
    c.set_static_mesh(cube)
    c.set_material(0,mats[material])
    c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    a.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)))
    count+=1
    return a

box('Floor',(0,0,-12),(3600,2800,24),'Concrete')
box('Wall_Back',(0,1400,300),(3620,20,600),'Wall')
box('Wall_Left',(-1800,0,300),(20,2800,600),'Wall')
box('Wall_Right',(1800,0,300),(20,2800,600),'Wall')
# Front loading doorway: 6 m clear opening.
for x in [-1050,1050]:
    box('Wall_Loading',(x,-1400,300),(1500,20,600),'Wall')
box('DoorLintel',(0,-1400,550),(600,24,100),'Steel')
for x in [-1700,1700]:
    for y in [-1300,0,1300]:
        box('Column',(x,y,300),(35,35,600),'Steel')
        box('ColumnGuard',(x,y,45),(55,55,90),'Yellow')
        box('RoofBeam',(0,y,590),(3400,25,35),'Steel') if x<0 else None
# Main aisle has 5 m clear width, with the forklift spawning in its centre.
for y in [-250,250]:
    box('LaneEdge',(0,y,1),(3300,8,2),'Yellow',False)
for x in range(-1500,1600,200):
    box('LaneDash',(x,0,1),(90,5,2),'White',False)
box('Walkway',(0,1220,1),(3300,120,2),'Walkway',False)
for y in [1155,1285]:
    box('WalkwayEdge',(0,y,2),(3300,5,2),'White',False)
# Two rack rows: each 3 bays, 300 cm wide and 110 cm deep.
for row,y in enumerate([-850,750]):
    for bay,x in enumerate([-1150,-800,-450]):
        tag='Rack_%d_%d'%(row,bay)
        for dx in [-150,150]:
            for dy in [-55,55]:
                box(tag+'_Post',(x+dx,y+dy,225),(10,10,450),'RackBlue')
        for z in [160,310,440]:
            for dy in [-55,55]:
                box(tag+'_Beam',(x,y+dy,z),(310,10,14),'SafetyOrange')
            box(tag+'_Shelf',(x,y,z+8),(300,110,3),'Steel')
        # Elevated stored loads are fixed scenery; ground bays stay open for operations.
        for dx in [-75,75]:
            box(tag+'_StoredPallet',(x+dx,y,181),(110,100,15),'Wood')
            box(tag+'_Crate',(x+dx,y,226),(96,90,75),'Wood')
        for dx in [-160,160]:
            box(tag+'_Guard',(x+dx,y-75,35),(20,20,70),'Yellow')
# Production zone and workbenches, away from the main aisle.
for i,x in enumerate([650,1250]):
    box('Machine_Base',(x,800,45),(260,210,90),'Steel')
    box('Machine_Housing',(x,830,155),(230,160,160),'Machine')
    box('Machine_Window',(x,745,175),(120,5,75),'Black')
    box('Machine_Control',(x+100,715,140),(40,30,65),'Wall')
    box('Machine_Screen',(x+100,698,150),(28,3,22),'Black')
    box('Workbench_Top',(x,450,90),(220,90,10),'Wood')
    for dx in [-95,95]:
        for dy in [-30,30]:
            box('Workbench_Leg',(x+dx,450+dy,42),(8,8,84),'Steel')
# Empty receiving bays sized to the 110 cm pallets, with room to approach.
for x in [450,850,1250]:
    for dx in [-85,85]:
        box('Receiving_Line',(x+dx,-800,1),(5,170,2),'Yellow',False)
    for y in [-885,-715]:
        box('Receiving_Line',(x,y,1),(170,5,2),'Yellow',False)
for x in [-1100,-650,-200,250,700,1150]:
    for j in range(6):
        box('Crossing',(x+j*40,-1190,1),(20,140,2),'White',False)
# Overhead luminaires; no solid ceiling, so the existing orbit camera remains usable.
for x in [-1200,0,1200]:
    for y in [-850,650]:
        box('LightHousing',(x,y,560),(180,40,10),'White',False)
        a=actors.spawn_actor_from_class(unreal.RectLight,unreal.Vector(ox+x,oy+y,550),unreal.Rotator(-90,0,0))
        a.set_actor_label('Factory_OverheadLight')
        a.set_folder_path('Factory/Lighting')
        c=a.get_component_by_class(unreal.RectLightComponent)
        c.set_editor_property('intensity',12000)
        c.set_editor_property('attenuation_radius',1800)
        c.set_editor_property('source_width',160)
        c.set_editor_property('source_height',30)
if not level.save_current_level():
    raise RuntimeError('Factory map save failed')
unreal.log('FACTORY_COMPLETE meshes=%d map=%s origin=(%s,%s)'%(count,target,ox,oy))
