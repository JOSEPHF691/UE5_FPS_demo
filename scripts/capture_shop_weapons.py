# Run in a separate full UE editor/RHI process (not a Python commandlet).
# The script exits that process after capture; do not run it in an interactive
# editor holding unsaved work.
# Output is a pair per weapon under Saved/CodexTemp/ShopRenders; alpha in the
# _InvAlpha image must be inverted and paired with the corresponding RGB image.
# A first pass primes the actual mesh/material/texture resources. Captures are
# made only on the second pass, after those resources have had time to compile.
"""Transient editor-only shop renders. No level/package is saved."""

import os

import unreal


if "-renderoffscreen" not in unreal.SystemLibrary.get_command_line().lower():
    raise RuntimeError("Shop capture only runs in a separate -RenderOffscreen editor process")

OUTPUT = os.path.join(unreal.Paths.project_dir(), "Saved", "CodexTemp", "ShopRenders")
os.makedirs(OUTPUT, exist_ok=True)
WEAPONS = (
    ("Pistol", "/Game/Weapons/Pistol/Meshes/SKM_Pistol", 45.0, "Y"),
    ("Rifle", "/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Meshes/SK_RAR_AssaultRifle", 105.0, "Y"),
    ("Sniper", "/Game/Tactical/Weapons/Sniper/SKM_TacticalSniper", 135.0, "X"),
)
RIFLE_PARTS = (
    ("Magazine", "/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Meshes/SM_RAR_AssaultRifle_Magazine_Default_Full", "SOCKET_Magazine", None),
    ("Forestock", "/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Meshes/SM_RAR_AssaultRifle_Forestock_Default", "SOCKET_Forestock", None),
    ("FrontSight", "/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Meshes/SM_RAR_AssaultRifle_IronSight_F", "", unreal.Vector(0, 44.092873, 7.492528)),
)
TEXTURE_DIRS = (
    "/Game/Weapons/Pistol/Textures",
    "/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Textures",
    "/Game/WeaponSniper/Texture",
)
WARMUP_START = 10
CAPTURE_START = 800
FRAMES_PER_WEAPON = 70


def note(message):
    unreal.log_warning("SHOP_RENDER " + str(message))


def plus(v, x, y, z):
    return unreal.Vector(v.x + x, v.y + y, v.z + z)


world = None
actors = []
target = None
capture = None
tick_count = 0
preloaded_assets = []


def make_light(center, offset, intensity):
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PointLight, plus(center, *offset), transient=True)
    light = actor.point_light_component
    light.set_editor_property("intensity", intensity)
    light.set_editor_property("attenuation_radius", 600.0)
    actors.append(actor)


def preload_weapon_resources():
    """Keep source meshes, material instances and textures alive during capture."""
    for _name, path, _width, _axis in WEAPONS:
        mesh = unreal.load_asset(path)
        assert mesh, "missing {}".format(path)
        preloaded_assets.append(mesh)
        for slot in mesh.get_editor_property("materials"):
            material = slot.get_editor_property("material_interface")
            assert material, "missing material slot on {}".format(path)
            preloaded_assets.append(material)
    for _label, path, _socket, _offset in RIFLE_PARTS:
        part = unreal.load_asset(path)
        assert part, "missing rifle part {}".format(path)
        preloaded_assets.append(part)
    for directory in TEXTURE_DIRS:
        for path in unreal.EditorAssetLibrary.list_assets(directory, recursive=True):
            asset = unreal.load_asset(path)
            if isinstance(asset, unreal.Texture):
                preloaded_assets.append(asset)
    note("preloaded {} mesh/material/texture assets".format(len(preloaded_assets)))


def start_weapon(name, path, ortho_width, long_axis):
    global world, capture, target
    world = unreal.EditorLevelLibrary.get_editor_world()
    mesh = unreal.load_asset(path)
    assert mesh, "missing {}".format(path)
    gun = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkeletalMeshActor, unreal.Vector(0, 0, 100), transient=True)
    actors.append(gun)
    gun.skeletal_mesh_component.set_skeletal_mesh_asset(mesh)
    show_actors = [gun]

    if name == "Rifle":
        for label, part_path, socket, local_offset in RIFLE_PARTS:
            part_mesh = unreal.load_asset(part_path)
            assert part_mesh, "missing rifle part {}".format(part_path)
            part = unreal.EditorLevelLibrary.spawn_actor_from_class(
                unreal.StaticMeshActor, unreal.Vector(0, 0, 100), transient=True)
            actors.append(part)
            part.static_mesh_component.set_static_mesh(part_mesh)
            part.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
            ok = part.static_mesh_component.attach_to_component(
                gun.skeletal_mesh_component, socket,
                unreal.AttachmentRule.SNAP_TO_TARGET,
                unreal.AttachmentRule.SNAP_TO_TARGET,
                unreal.AttachmentRule.SNAP_TO_TARGET)
            assert ok, "attach {} failed".format(label)
            if local_offset:
                part.static_mesh_component.set_relative_location(local_offset, False, False)
            show_actors.append(part)
            note("{} socket={} loc={}".format(label, socket, part.get_actor_location()))

    # Include every detachable part in the framing bounds. The receiver's own
    # bounds do not include the separately spawned front sight near the muzzle.
    mins = [float("inf")] * 3
    maxs = [float("-inf")] * 3
    for actor in show_actors:
        part_center, part_extent = actor.get_actor_bounds(False)
        for axis, c, e in ((0, part_center.x, part_extent.x),
                           (1, part_center.y, part_extent.y),
                           (2, part_center.z, part_extent.z)):
            mins[axis] = min(mins[axis], c - e)
            maxs[axis] = max(maxs[axis], c + e)
    center = unreal.Vector(*[(low + high) * 0.5 for low, high in zip(mins, maxs)])
    extent = unreal.Vector(*[(high - low) * 0.5 for low, high in zip(mins, maxs)])
    if long_axis == "Y":
        camera_loc = plus(center, 160, -35, 34)
    else:
        camera_loc = plus(center, -35, 160, 34)
    make_light(center, (55, -42, 68), 50.0)
    make_light(center, (-50, 35, 38), 25.0)

    capture = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SceneCapture2D, camera_loc, transient=True)
    actors.append(capture)
    capture.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(
        camera_loc, center), False)
    comp = capture.capture_component2d
    comp.set_editor_property("capture_every_frame", False)
    comp.set_editor_property("capture_on_movement", False)
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property("primitive_render_mode", unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    comp.set_editor_property("consider_unrendered_opaque_pixel_as_fully_translucent", True)
    comp.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
    comp.set_editor_property("ortho_width", ortho_width)
    for actor in show_actors:
        comp.show_only_actor_components(actor)
    pp = comp.get_editor_property("post_process_settings")
    pp.set_editor_property("override_auto_exposure_method", True)
    pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    pp.set_editor_property("override_auto_exposure_bias", True)
    pp.set_editor_property("auto_exposure_bias", 0.0)
    comp.set_editor_property("post_process_settings", pp)
    comp.set_editor_property("post_process_blend_weight", 1.0)
    target = unreal.RenderingLibrary.create_render_target2d(
        world, 1024, 576, unreal.TextureRenderTargetFormat.RTF_RGBA8,
        unreal.LinearColor(0, 0, 0, 0))
    comp.set_editor_property("texture_target", target)
    comp.capture_scene()
    note("{} RGB capture; bounds={} ortho_width={}".format(name, (center, extent), ortho_width))


def end_weapon():
    global target, capture
    for actor in reversed(actors):
        if actor:
            actor.destroy_actor()
    actors.clear()
    target = None
    capture = None


def tick(_delta):
    global tick_count
    tick_count += 1
    try:
        if tick_count == 1:
            preload_weapon_resources()
        for index, (name, path, width, axis) in enumerate(WEAPONS):
            warmup = WARMUP_START + index * FRAMES_PER_WEAPON
            if tick_count == warmup:
                start_weapon(name, path, width, axis)
                note("{} material warm-up started".format(name))
            elif tick_count == warmup + 50:
                end_weapon()

            start = CAPTURE_START + index * FRAMES_PER_WEAPON
            if tick_count == start:
                start_weapon(name, path, width, axis)
            elif tick_count == start + 20:
                unreal.RenderingLibrary.export_render_target(world, target, OUTPUT, name + "_RGB.png")
                note("{} RGB exported".format(name))
                capture.capture_component2d.set_editor_property(
                    "capture_source", unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR)
                capture.capture_component2d.capture_scene()
            elif tick_count == start + 40:
                unreal.RenderingLibrary.export_render_target(world, target, OUTPUT, name + "_InvAlpha.png")
                note("{} inverted-alpha exported".format(name))
            elif tick_count == start + 50:
                end_weapon()
        if tick_count == CAPTURE_START + len(WEAPONS) * FRAMES_PER_WEAPON:
            note("COMPLETE")
            unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")
    except Exception as exc:
        note("ERROR tick={} {!r}".format(tick_count, exc))
        if world:
            unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")


unreal.register_slate_post_tick_callback(tick)
