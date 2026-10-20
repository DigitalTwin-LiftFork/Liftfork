import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/DigitalTwin/Factory')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
forks=[a for a in actors if a.get_class().get_name()=='AutomatedForklift']
pallets=[a for a in actors if a.get_class().get_name()=='Pallet']
floor=next(a for a in actors if a.get_actor_label()=='Factory_Floor')
roof=next(a for a in actors if a.get_actor_label()=='Factory_Roof')
assert len(forks)==1 and len(pallets)==114
assert abs(floor.get_actor_scale3d().x-144)<.01
assert abs(floor.get_actor_scale3d().y-112)<.01
assert abs(roof.get_actor_location().z-1510)<.01
loaded=[a for a in pallets if a.get_editor_property('has_cargo')]
assert 0 < len(loaded) < len(pallets)
assert all(a.get_editor_property('cargo_visual').is_visible() for a in loaded)
unreal.log('FACTORY_VERIFIED forklift=1 pallets=114 loaded=%d floor=144x112m roof underside=15m'%len(loaded))
