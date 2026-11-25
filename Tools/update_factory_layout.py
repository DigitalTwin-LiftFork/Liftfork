"""Persist the single-storey factory zones and remove empty shelf pallets.

Run through UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Only actors tagged FactoryZoneLayout are replaced; occupied pallets are retained.
"""
import json
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/DigitalTwin/Factory')
all_actors = editor.get_all_level_actors()
tag = 'FactoryZoneLayout'
for actor in all_actors:
    if tag in [str(value) for value in actor.tags]:
        editor.destroy_actor(actor)
all_actors = editor.get_all_level_actors()
floor_actor = next(a for a in all_actors if a.get_actor_label() == 'Factory_Floor')
origin, extent = floor_actor.get_actor_bounds(False)
floor = origin.z + extent.z
front_y = origin.y - extent.y + 1300
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
materials = {name: unreal.load_asset('/Game/DigitalTwin/FactoryAssets/M_'+name)
             for name in ['Yellow', 'Walkway', 'Steel', 'SafetyOrange']}
counts = {'signs': 0, 'meshes': 0, 'removed_empty_shelf_pallets': 0}


def setup(actor, label):
    actor.set_actor_label('FactoryZone_'+label)
    actor.set_folder_path('Factory/Zones')
    actor.tags = [unreal.Name(tag)]
    return actor


def box(label, x, y, z, sx, sy, sz, material='Yellow', collision=False):
    actor = setup(editor.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x,y,z)), label)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_material(0, materials[material])
    actor.static_mesh_component.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    actor.set_actor_scale3d(unreal.Vector(sx/100,sy/100,sz/100))
    counts['meshes'] += 1


def sign(text, x, y, z, color, size=48):
    actor = setup(editor.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(x,y,z), unreal.Rotator(pitch=0,yaw=90,roll=0)), text)
    actor.set_actor_rotation(unreal.Rotator(pitch=0,yaw=90,roll=0), False)
    component = actor.get_component_by_class(unreal.TextRenderComponent)
    component.set_text(text)
    component.set_world_size(size)
    component.set_text_render_color(unreal.Color(*color))
    component.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    counts['signs'] += 1


zones = [
    ('INBOUND',-5500,(0,255,255,255)),
    ('INSPECTION',-3400,(255,165,0,255)),
    ('PRODUCTION',-1300,(0,255,0,255)),
    ('OUTBOUND',2500,(255,255,0,255)),
    ('CHARGING',5100,(100,160,255,255)),
]
for index,(name,xoffset,color) in enumerate(zones):
    x,y=origin.x+xoffset,front_y
    sign(name,x,y-550,floor+200,color)
    for edge in range(4):
        across=edge<2
        box(name+'_Boundary_'+str(edge),x+(0 if across else -700 if edge==2 else 700),
            y+(-700 if edge==0 else 700 if edge==1 else 0),floor+1,
            1400 if across else 8,8 if across else 1400,2,
            'Walkway' if index==4 else 'Yellow')
    bay_count=1 if index==4 else 2
    for bay in range(bay_count):
        bx=x+(0 if bay_count==1 else -300 if bay==0 else 300)
        halfwidth=180 if bay_count==1 else 90
        for side in [-1,1]:
            box(name+'_BayEdge',bx+side*halfwidth,y+100,floor+2,5,400,2)
            box(name+'_BayEdge',bx,y+100+side*200,floor+2,halfwidth*2,5,2)
        sign(name+'-%02d'%(bay+1),bx,y+320,floor+70,color,24)
    if index in [1,2]:
        box(name+'_Workbench',x,y-500,floor+55,350,100,110,'Steel',True)
    if index==4:
        box(name+'_Charger',x,y-500,floor+65,100,60,130,'Steel',True)
        box(name+'_ChargerPanel',x,y-467,floor+90,70,4,45,'Walkway')
        box(name+'_StatusLight',x,y-500,floor+140,22,22,15,'Walkway')
        box(name+'_DockPad',x,y+100,floor+1.2,340,380,1,'Walkway')
        # Conductive ground contacts; the painted rectangle itself supplies no power.
        for side in [-1,1]:
            box(name+'_ContactRail',x+side*30,y+100,floor+4,12,80,4,'SafetyOrange')
        box(name+'_DockArrow',x,y+240,floor+2,8,100,2,'Yellow')
        box(name+'_CableConduit',x,y-190,floor+2,8,580,2,'Steel')
        for side in [-1,1]:
            box(name+'_GuardPost',x+side*230,y-500,floor+45,20,20,90,'Yellow',True)

for rows,text,color in [('012','RAW MATERIALS / A-C',(0,255,255,255)),('345','FINISHED GOODS / D-F',(255,255,0,255))]:
    bounds=[]
    for actor in all_actors:
        label=actor.get_actor_label()
        if any(label.startswith('Factory_Rack_'+row+'_') for row in rows):
            center,half=actor.get_actor_bounds(False)
            bounds.append((center,half))
    assert bounds
    min_x=min(c.x-e.x for c,e in bounds);max_x=max(c.x+e.x for c,e in bounds)
    min_y=min(c.y-e.y for c,e in bounds)
    sign(text,(min_x+max_x)/2,min_y-100,floor+200,color)

# Preserve transport identity before deleting actors; runtime IDs must not shift
# when the sorted actor list becomes shorter.
identity_actors=sorted([a for a in all_actors if a.get_class().get_name() in ['Pallet','WoodPallet']],key=lambda a:a.get_name())
used={str(a.get_editor_property('pallet_id')) for a in identity_actors if str(a.get_editor_property('pallet_id'))!='None'}
claimed=set()
for actor in identity_actors:
    pallet_id=str(actor.get_editor_property('pallet_id'))
    if pallet_id!='None' and pallet_id not in claimed:
        claimed.add(pallet_id)
        continue
    number=1
    while 'P-%03d'%number in used:
        number+=1
    pallet_id='P-%03d'%number
    actor.set_editor_property('pallet_id',unreal.Name(pallet_id))
    used.add(pallet_id)
    claimed.add(pallet_id)
decks=[]
for actor in all_actors:
    label=actor.get_actor_label()
    if label.startswith('Factory_Rack_') and '_Deck' in label:
        c,e=actor.get_actor_bounds(False)
        decks.append((c.x-e.x,c.x+e.x,c.y-e.y,c.y+e.y,c.z+e.z))
assert len(decks)==216, 'Unexpected rack layout; not removing pallets'
pallets=[a for a in all_actors if a.get_class().get_name()=='Pallet']
loaded_before=sum(bool(a.get_editor_property('has_cargo')) for a in pallets)
shelf_loaded_before=sum(1 for a in pallets if a.get_editor_property('has_cargo') and any(x0<=a.get_actor_location().x<=x1 and y0<=a.get_actor_location().y<=y1 and abs(a.get_actor_location().z-z)<=25 for x0,x1,y0,y1,z in decks))
for actor in pallets:
    if actor.get_editor_property('has_cargo'):
        continue
    p=actor.get_actor_location()
    if any(x0<=p.x<=x1 and y0<=p.y<=y1 and abs(p.z-z)<=25 for x0,x1,y0,y1,z in decks):
        assert editor.destroy_actor(actor)
        counts['removed_empty_shelf_pallets']+=1
# Remove authored floor pickup examples and their old staging marks, retaining
# all shelf loads and any unrelated user-authored actors.
for actor in editor.get_all_level_actors():
    label=actor.get_actor_label()
    if actor.get_class().get_name()=='Pallet' and label.startswith(('Factory_LoadedPallet_','Factory_EmptyPallet_')):
        p=actor.get_actor_location()
        on_shelf=any(x0<=p.x<=x1 and y0<=p.y<=y1 and abs(p.z-z)<=25 for x0,x1,y0,y1,z in decks)
        if not on_shelf:
            editor.destroy_actor(actor)
            counts['removed_floor_examples']=counts.get('removed_floor_examples',0)+1
    elif label.startswith('Factory_Staging'):
        editor.destroy_actor(actor)
remaining=[a for a in editor.get_all_level_actors() if a.get_class().get_name()=='Pallet']
assert sum(bool(a.get_editor_property('has_cargo')) for a in remaining)==shelf_loaded_before
assert counts['signs']==16
for actor in editor.get_all_level_actors():
    if actor.get_class().get_name()=='AutomatedForklift':
        actor.get_editor_property('transport_coordinator').set_editor_property('battery_time_scale',1.0)
assert level.save_current_level()
counts.update(remaining_pallets=len(remaining),loaded_pallets=sum(bool(a.get_editor_property('has_cargo')) for a in remaining),deck_count=len(decks))
unreal.log('FACTORY_ZONE_LAYOUT_SAVED '+json.dumps(counts))
