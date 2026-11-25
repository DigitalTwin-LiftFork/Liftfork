import json
import unreal

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/DigitalTwin/Factory')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
signs=[a for a in actors if 'FactoryZoneLayout' in [str(t) for t in a.tags]
       and isinstance(a,unreal.TextRenderActor)]
assert len(signs)==16, len(signs)
assert len({(a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z) for a in signs})==16
assert all(abs(a.get_actor_rotation().yaw-90)<.1 for a in signs)
decks=[]
for a in actors:
    if a.get_actor_label().startswith('Factory_Rack_') and '_Deck' in a.get_actor_label():
        c,e=a.get_actor_bounds(False)
        decks.append((c.x-e.x,c.x+e.x,c.y-e.y,c.y+e.y,c.z+e.z))
assert len(decks)==216
pallets=[a for a in actors if a.get_class().get_name()=='Pallet']
loaded=[a for a in pallets if a.get_editor_property('has_cargo')]
empty=[a for a in pallets if not a.get_editor_property('has_cargo')]
for a in empty:
    p=a.get_actor_location()
    assert not any(x0<=p.x<=x1 and y0<=p.y<=y1 and abs(p.z-z)<=25 for x0,x1,y0,y1,z in decks),a.get_actor_label()
assert loaded
assert len(pallets)==54 and not empty
assert all(a.get_editor_property('cargo_visual').is_visible() for a in loaded)
assert len([a for a in actors if a.get_class().get_name()=='AutomatedForklift'])==1
unreal.log('FACTORY_LAYOUT_VERIFIED '+json.dumps(dict(signs=len(signs),shelf_decks=len(decks),pallets=len(pallets),loaded=len(loaded),empty_floor_pallets=len(empty))))
