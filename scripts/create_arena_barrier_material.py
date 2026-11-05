"""Create the dedicated, light translucent treasure-arena side-panel material.

Run with UnrealEditor-Cmd -run=pythonscript. Never edits M_Shield, which is also
used for player armor, or the level containing the authored arena.
"""

import unreal


ASSET_PATH = "/Game/Variant_Shooter/Materials/M_TacticalArenaBarrier"
if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
    unreal.log(f"ARENA_MATERIAL already exists: {ASSET_PATH}")
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(
        "M_TacticalArenaBarrier",
        "/Game/Variant_Shooter/Materials",
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )
    if not material:
        raise RuntimeError("Could not create tactical arena barrier material")

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -350, -100
    )
    color.set_editor_property("parameter_name", "BarrierColor")
    color.set_editor_property("default_value", unreal.LinearColor(0.55, 0.9, 1.0, 1.0))
    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -350, 120
    )
    opacity.set_editor_property("parameter_name", "BarrierOpacity")
    opacity.set_editor_property("default_value", 0.13)

    if not unreal.MaterialEditingLibrary.connect_material_property(
        color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError("Could not connect arena barrier color")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY
    ):
        raise RuntimeError("Could not connect arena barrier opacity")

    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Could not save tactical arena barrier material")
    unreal.log(f"ARENA_MATERIAL created: {ASSET_PATH} color=(0.55,0.9,1.0) opacity=0.13")
