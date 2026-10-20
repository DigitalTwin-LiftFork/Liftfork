import unreal, re
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/DigitalTwin/Factory')
all_actors=actors.get_all_level_actors()
start=next(a for a in all_actors if a.get_actor_label()=='Factory_ForkliftStart')
origin=start.get_actor_location()
ox,oy=origin.x,origin.y
for a in all_actors:
    label=a.get_actor_label()
    if not label.startswith('Factory_') or a==start:
        continue
    p=a.get_actor_location()
    rack=re.match(r'Factory_Rack_(\d)_(\d)',label)
    if rack:
        cx=[-1150,-800,-450][int(rack[2])]
        cy=[-850,750][int(rack[1])]
        newpos=unreal.Vector(p.x+cx,p.y+cy,p.z)
    elif label.startswith(('Factory_Machine_','Factory_Workbench_')):
        cx=min([650,1250],key=lambda x:abs(p.x-ox-x))
        cy=800 if label.startswith('Factory_Machine_') else 450
        newpos=unreal.Vector(p.x+cx,p.y+cy,p.z)
    elif label.startswith('Factory_Receiving_'):
        cx=min([450,850,1250],key=lambda x:abs(p.x-ox-x))
        newpos=unreal.Vector(p.x+cx,p.y-800,p.z)
    else:
        newpos=unreal.Vector(ox+2*(p.x-ox),oy+2*(p.y-oy),p.z)
    a.set_actor_location(newpos,False,False)
    s=a.get_actor_scale3d()
    if label=='Factory_Floor':
        a.set_actor_scale3d(unreal.Vector(s.x*2,s.y*2,s.z))
    elif label.startswith('Factory_Wall'):
        a.set_actor_scale3d(unreal.Vector(s.x*2,s.y*2,s.z*2.5))
        p=a.get_actor_location(); a.set_actor_location(unreal.Vector(p.x,p.y,750),False,False)
    elif label=='Factory_Column':
        a.set_actor_scale3d(unreal.Vector(s.x,s.y,s.z*2.5))
        p=a.get_actor_location(); a.set_actor_location(unreal.Vector(p.x,p.y,750),False,False)
    elif label.startswith('Factory_RoofBeam'):
        a.set_actor_scale3d(unreal.Vector(s.x*2,s.y,s.z))
        p=a.get_actor_location(); a.set_actor_location(unreal.Vector(p.x,p.y,1480),False,False)
    elif label.startswith('Factory_DoorLintel'):
        a.set_actor_scale3d(unreal.Vector(s.x*2,s.y,s.z))
        p=a.get_actor_location(); a.set_actor_location(unreal.Vector(p.x,p.y,1450),False,False)
    elif any(label.startswith('Factory_'+n) for n in ['Walkway','LaneEdge']):
        a.set_actor_scale3d(unreal.Vector(s.x*2,s.y,s.z))
    elif label.startswith('Factory_LightHousing'):
        p=a.get_actor_location(); a.set_actor_location(unreal.Vector(p.x,p.y,1440),False,False)
    elif isinstance(a,unreal.RectLight):
        p=a.get_actor_location(); a.set_actor_location(unreal.Vector(p.x,p.y,1420),False,False)
        c=a.get_component_by_class(unreal.RectLightComponent)
        c.set_editor_property('intensity',60000)
        c.set_editor_property('attenuation_radius',4000)
# Actual roof slab with collision; underside is at 15 metres.
roof=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(ox,oy,1510))
roof.set_actor_label('Factory_Roof')
roof.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
roof.static_mesh_component.set_material(0,unreal.load_asset('/Game/DigitalTwin/FactoryAssets/M_Wall'))
roof.static_mesh_component.set_collision_profile_name('BlockAll')
roof.set_actor_scale3d(unreal.Vector(72,56,.2))
forklift=actors.spawn_actor_from_class(unreal.load_class(None,'/Script/DigitalTwin.AutomatedForklift'),unreal.Vector(ox,oy,55),unreal.Rotator())
forklift.set_actor_label('Factory_Forklift')
forklift.set_folder_path('Factory/Vehicles')
# Existing physical pallet actors, with clear approach lanes.
for i,(x,y) in enumerate([(650,0),(1100,-1600),(1700,-1600),(2300,-1600),(1100,-2050),(1700,-2050)]):
    pallet=actors.spawn_actor_from_class(unreal.load_class(None,'/Script/DigitalTwin.Pallet'),unreal.Vector(ox+x,oy+y,1),unreal.Rotator())
    pallet.set_actor_label('Factory_TransportPallet_%02d'%i)
    pallet.set_folder_path('Factory/TransportPallets')
assert level.save_current_level(), 'Factory save failed'
unreal.log('FACTORY_EXPANDED 72x56m height=15m forklift=1 physical_pallets=6')
