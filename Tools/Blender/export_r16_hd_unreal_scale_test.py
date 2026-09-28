"""Export one R16 HD source part with explicit centimetre geometry for Unreal validation."""
import bpy
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "Content", "DigitalTwin", "Art", "R16HD")

bpy.ops.object.select_all(action="DESELECT")
collection = bpy.data.collections["ForkLeft_Actual"]
collection.hide_viewport = False
for obj in collection.objects:
    obj.hide_set(False)
    obj.select_set(True)
bpy.context.view_layer.objects.active = next(iter(collection.objects))

bpy.ops.export_scene.fbx(
    filepath=os.path.join(OUT, "ForkLeft_UEcmTest.fbx"),
    use_selection=True,
    object_types={'MESH'},
    apply_unit_scale=False,
    apply_scale_options='FBX_SCALE_NONE',
    global_scale=1.0,
    axis_forward='-Z',
    axis_up='Y',
    add_leaf_bones=False,
    mesh_smooth_type='FACE',
)
