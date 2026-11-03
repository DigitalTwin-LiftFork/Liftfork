import unreal, math
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/DigitalTwin/Factory')
existing=actors.get_all_level_actors()
fork=next(a for a in existing if a.get_class().get_name()=='AutomatedForklift')
o=fork.get_actor_location()
ox,oy=o.x,o.y
for a in existing:
    if a.get_actor_label().startswith('Factory_') and a!=fork and not isinstance(a,unreal.PlayerStart):
        actors.destroy_actor(a)
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
mats={n:unreal.load_asset('/Game/DigitalTwin/FactoryAssets/M_'+n) for n in ['Concrete','Wall','Steel','RackBlue','SafetyOrange','Yellow','Wood','White','Walkway']}
def box(name,x,y,z,sx,sy,sz,mat,collision=True):
    a=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(ox+x,oy+y,z))
    a.set_actor_label('Factory_'+name)
    a.set_folder_path('Factory/'+name.split('_')[0])
    a.static_mesh_component.set_static_mesh(cube)
    a.static_mesh_component.set_material(0,mats[mat])
    a.static_mesh_component.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    a.set_actor_scale3d(unreal.Vector(sx/100,sy/100,sz/100))
    return a
box('Floor',0,0,-12,14400,11200,24,'Concrete')
box('Roof',0,0,1510,14400,11200,20,'Wall')
for x in [-7200,7200]: box('Wall',x,0,750,20,11200,1500,'Wall')
box('Wall',0,5600,750,14400,20,1500,'Wall')
for x in [-3900,3900]: box('Wall',x,-5600,750,6600,20,1500,'Wall')
box('DoorLintel',0,-5600,1400,1200,25,200,'Steel')
for x in [-6900,6900]:
    for y in range(-5000,5001,2000):
        box('Column',x,y,750,40,40,1500,'Steel')
        box('Guard',x,y,50,65,65,100,'Yellow')
        if x<0: box('RoofBeam',0,y,1470,13800,30,40,'Steel')
# Main cross aisle and long clear approaches to every rack face.
for x in [-600,600]: box('Lane',x,0,1,8,10500,2,'Yellow',False)
for y in [-2500,2500]: box('Lane',0,y,1,13500,8,2,'Yellow',False)
box('Walkway',6500,0,1,160,10400,2,'Walkway',False)
for x in [6410,6590]: box('WalkwayEdge',x,0,2,5,10400,2,'White',False)
pallet_class=unreal.load_class(None,'/Script/DigitalTwin.Pallet')
count=0
def pallet(x,y,z,loaded,index):
    global count
    a=actors.spawn_actor_from_class(pallet_class,unreal.Vector(ox+x,oy+y,z),unreal.Rotator())
    a.set_editor_property('has_cargo',loaded)
    # Construction is rerun explicitly by changing the transform.
    a.set_actor_location(unreal.Vector(ox+x,oy+y,z),False,False)
    cargo=a.get_editor_property('cargo_visual')
    cargo.set_visibility(loaded)
    cargo.set_material(0,mats['Wood'])
    cc=a.get_editor_property('cargo_collision')
    cc.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS if loaded else unreal.CollisionEnabled.NO_COLLISION)
    a.set_actor_label('Factory_%sPallet_%03d'%('Loaded' if loaded else 'Empty',index))
    a.set_folder_path('Factory/TransportPallets')
    count+=1
for row,x in enumerate([-4500,-3000,-1500,1500,3000,4500]):
    for bay in range(12):
        y=-2040+bay*340
        tag='Rack_%d_%02d'%(row,bay)
        for dx in [-60,60]:
            for dy in [-150,150]:
                box(tag+'_Post',x+dx,y+dy,250,10,10,500,'RackBlue')
        for z in [80,230,380]:
            for dx in [-60,60]: box(tag+'_Beam',x+dx,y,z-5,12,310,10,'SafetyOrange')
            box(tag+'_Deck',x,y,z-1.5,120,300,3,'Steel')
        # Side-frame diagonals in X/Z, preserving access through the long faces.
        for dy in [-150,150]:
            for z in [90,250,410]:
                brace=box(tag+'_Brace',x,y+dy,z,math.sqrt(120**2+140**2),4,4,'RackBlue')
                brace.set_actor_rotation(unreal.Rotator(math.degrees(math.atan2(140,120)),0,0),False)
        for dx in [-70,70]:
            box(tag+'_Protector',x+dx,y-160,35,18,18,70,'Yellow')
        for tier,z in enumerate([80,230,380]):
            if (row+bay+tier)%2==0:
                pallet(x,y,z+1.5,(row+bay+tier)%4==0,count)
# Floor-level pickup examples facing the forklift and staging area.
for i,(x,y) in enumerate([(650,0),(1100,3000),(1500,3000),(1900,3000),(1100,3500),(1500,3500)]):
    pallet(x,y,1.5,i%2==0,count)
    for dy in [-75,75]: box('Staging',x,y+dy,1,150,5,2,'Yellow',False)
for x in [-5400,-2700,0,2700,5400]:
    for y in [-4200,-1400,1400,4200]:
        box('Luminaire',x,y,1440,180,40,10,'White',False)
        a=actors.spawn_actor_from_class(unreal.RectLight,unreal.Vector(ox+x,oy+y,1420),unreal.Rotator(-90,0,0))
        a.set_actor_label('Factory_Light')
        c=a.get_component_by_class(unreal.RectLightComponent)
        c.set_editor_property('intensity',80000)
        c.set_editor_property('attenuation_radius',4300)
assert level.save_current_level()
unreal.log('RACK_FACTORY_SAVED footprint=144x112m racks=72 shelf_tops=80,230,380cm pallets=%d'%count)
