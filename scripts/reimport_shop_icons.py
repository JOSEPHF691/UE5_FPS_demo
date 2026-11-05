"""Reimport only the three already-bound shop textures in a separate editor.

The DataTable is read for verification but is never modified or saved here.
Use -RenderOffscreen -ExecCmds="py <this-script>" with a separate editor process.
"""

import json
import os

import unreal


if "-renderoffscreen" not in unreal.SystemLibrary.get_command_line().lower():
    raise RuntimeError("Shop texture reimport only runs in a separate -RenderOffscreen editor process")

SOURCE_DIR = os.path.join(unreal.Paths.project_dir(), "SourceArt", "ShopIcons")
DEST_DIR = "/Game/Variant_Shooter/UI/ShopIcons"
TABLE_PATH = "/Game/Variant_Shooter/Blueprints/Pickups/Data/DT_TacticalWeaponData"
ITEMS = {
    "UpgradedPistol": "T_Shop_UpgradedPistol",
    "Rifle": "T_Shop_Rifle",
    "Sniper": "T_Shop_Sniper",
}


def reimport():
    table = unreal.EditorAssetLibrary.load_asset(TABLE_PATH)
    assert isinstance(table, unreal.DataTable), TABLE_PATH
    table_before = table.export_to_json_string()
    rows = json.loads(table_before)
    selected = {row["Tier"]: row for row in rows if row.get("Tier") in ITEMS}
    if set(selected) != set(ITEMS):
        raise RuntimeError("The three shop tiers are missing or duplicated")

    tasks = []
    for tier, name in ITEMS.items():
        path = DEST_DIR + "/" + name
        filename = os.path.join(SOURCE_DIR, name + ".png")
        texture = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(texture, unreal.Texture2D) or not os.path.isfile(filename):
            raise RuntimeError("Missing existing Texture2D or source PNG: " + path)
        if path not in str(selected[tier].get("Icon")):
            raise RuntimeError("DataTable icon reference is not the expected texture: " + tier)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", filename)
        task.set_editor_property("destination_path", DEST_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", False)
        tasks.append(task)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for tier, name in ITEMS.items():
        path = DEST_DIR + "/" + name
        texture = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError("Reimport did not produce a Texture2D: " + path)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("srgb", True)
        if hasattr(unreal.TextureMipGenSettings, "TMGS_NO_MIPMAPS"):
            texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        if not unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False):
            raise RuntimeError("Failed to save shop Texture2D: " + path)
        unreal.log_warning("SHOP_ICON_REIMPORTED {} {}".format(tier, texture.get_path_name()))

    if table.export_to_json_string() != table_before:
        raise RuntimeError("Unexpected in-memory DataTable change")
    unreal.log_warning("SHOP_ICON_REIMPORT_SUCCESS; DataTable references unchanged")


try:
    reimport()
finally:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")
