"""The calls of the driver to the Python API of the Unreal editor (UE 5.8), in one place: the
Terrain World of the level, Play In Editor, the camera, the screenshots, the pause, the pawn and
the trace down. Every call checks what the API offers and says clearly what is missing
(MissingApi): the names of UE 5.8 cannot be checked outside Unreal.

Coordinates: Unreal centimeters, X = 100 (x - ox), Y = 100 (z - oz), Z = 100 (y - oy) in the
frame of CoreOriginX/Z (places.core_to_unreal). Rotations: pitch positive up, yaw = heading - 90.
"""

import os
from pathlib import Path


class MissingApi(RuntimeError):
    """A function or a class of the Python API of Unreal that this editor does not have."""


def _get(owner, name, what=None):
    value = getattr(owner, name, None)
    if value is None:
        raise MissingApi(f"unreal API missing: {what or name}")
    return value


# What the driver calls, checked all at once before it starts ("a|b": one of them is enough).
# The functions of an actor are checked on its class.
NEEDED = {
    "editor": ["get_editor_subsystem", "register_slate_post_tick_callback",
               "UnrealEditorSubsystem.get_editor_world", "UnrealEditorSubsystem.get_game_world",
               "LevelEditorSubsystem.editor_request_begin_play",
               "LevelEditorSubsystem.editor_request_end_play",
               "LevelEditorSubsystem.is_in_play_in_editor",
               "GameplayStatics.get_all_actors_of_class", "GameplayStatics.get_player_pawn",
               "GameplayStatics.get_player_controller", "SystemLibrary.execute_console_command",
               "SystemLibrary.quit_editor", "Paths.screen_shot_dir",
               "Paths.convert_relative_path_to_full", "Vector", "Rotator",
               "PlayerController.set_view_target_with_blend",
               "PlayerCameraManager.get_camera_rotation"],
    "camera": ["CameraActor",
               "EditorActorSubsystem.spawn_actor_from_class|EditorLevelLibrary"
               ".spawn_actor_from_class",
               "EditorActorSubsystem.destroy_actor|Actor.destroy_actor",
               "GameplayStatics.get_all_actors_with_tag", "Actor.set_actor_location_and_rotation"],
    "walk": ["Pawn.add_movement_input", "Actor.set_actor_location",
             "Controller.set_control_rotation", "SystemLibrary.line_trace_single",
             "TraceTypeQuery", "DrawDebugTrace"],
    "sun": ["DirectionalLight", "Actor.set_actor_rotation"],
}


class Api:
    def __init__(self, unreal):
        self.ue = unreal
        self.missing = []

    def has(self, path):
        """Whether the API has a dotted name ("GameplayStatics.set_game_paused")."""
        owner = self.ue
        for part in path.split("."):
            owner = getattr(owner, part, None)
            if owner is None:
                return False
        return True

    def missing_of(self, groups):
        """Every name of these groups of NEEDED the API lacks, all at once."""
        out = []
        for group in groups:
            for spec in NEEDED[group]:
                if not any(self.has(name) for name in spec.split("|")):
                    out.append(spec)
        return out

    # ---- editor and Play In Editor ------------------------------------------------------------

    def subsystem(self, name):
        cls = _get(self.ue, name)
        return _get(self.ue, "get_editor_subsystem")(cls)

    def editor_world(self):
        """The world of the editor, or None while the editor starts (no subsystem nor world
        yet)."""
        subsystem = self.subsystem("UnrealEditorSubsystem")
        return subsystem.get_editor_world() if subsystem is not None else None

    def game_world(self):
        """The world of the Play In Editor, or None outside it."""
        try:
            return self.subsystem("UnrealEditorSubsystem").get_game_world()
        except MissingApi:
            raise
        except Exception:  # noqa: BLE001 - no game world yet
            return None

    def begin_play(self):
        self.subsystem("LevelEditorSubsystem").editor_request_begin_play()

    def end_play(self):
        self.subsystem("LevelEditorSubsystem").editor_request_end_play()

    def playing(self):
        return bool(self.subsystem("LevelEditorSubsystem").is_in_play_in_editor())

    def terrain_worlds(self, world):
        cls = getattr(self.ue, "TerrainWorld", None)
        if cls is None:
            raise MissingApi("unreal.TerrainWorld (is the plugin TerrainEngine loaded?)")
        return list(_get(self.ue, "GameplayStatics").get_all_actors_of_class(world, cls))

    def console(self, world, command):
        _get(self.ue, "SystemLibrary").execute_console_command(world, command)

    # ---- the Terrain World of the level -------------------------------------------------------

    @staticmethod
    def read(actor, name):
        return actor.get_editor_property(name)

    @staticmethod
    def write(actor, name, value):
        actor.set_editor_property(name, value)

    # ---- camera, sun, pause --------------------------------------------------------------------

    def rotator(self, pitch, yaw):
        return _get(self.ue, "Rotator")(roll=0.0, pitch=float(pitch), yaw=float(yaw))

    def vector(self, x, y, z):
        return _get(self.ue, "Vector")(float(x), float(y), float(z))

    def spawn_editor_camera(self, location, rotation, tag):
        """A CameraActor in the world of the editor (the deferred spawns of the world of a play
        are not exposed to Python in UE 5.8): Play In Editor duplicates it with the level, and
        find_tagged finds the copy by its tag. Never saved; destroy_editor_actor removes it."""
        cls = _get(self.ue, "CameraActor")
        spawner = getattr(self.ue, "EditorActorSubsystem", None)
        if spawner is not None and hasattr(spawner, "spawn_actor_from_class"):
            camera = self.subsystem("EditorActorSubsystem").spawn_actor_from_class(
                cls, location, rotation)
        else:
            library = _get(self.ue, "EditorLevelLibrary", "EditorActorSubsystem or "
                           "EditorLevelLibrary.spawn_actor_from_class")
            camera = library.spawn_actor_from_class(cls, location, rotation)
        camera.set_editor_property("tags", [tag])
        if hasattr(camera, "set_actor_label"):
            camera.set_actor_label(tag)
        return camera

    def find_tagged(self, world, tag):
        actors = list(_get(self.ue, "GameplayStatics").get_all_actors_with_tag(world, tag))
        return actors[0] if actors else None

    def destroy_editor_actor(self, actor):
        spawner = getattr(self.ue, "EditorActorSubsystem", None)
        if spawner is not None and hasattr(spawner, "destroy_actor"):
            self.subsystem("EditorActorSubsystem").destroy_actor(actor)
        else:
            actor.destroy_actor()

    def view_through(self, world, actor):
        controller = _get(self.ue, "GameplayStatics").get_player_controller(world, 0)
        controller.set_view_target_with_blend(actor, 0.0)

    def set_focus(self, world, actor):
        """The actor the tiles of the Terrain World of the play stream around (None: the pawn)."""
        for terrain in self.terrain_worlds(world):
            terrain.set_editor_property("focus_actor", actor)

    @staticmethod
    def teleport(actor, location):
        actor.set_actor_location(location, False, True)

    def place_camera(self, camera, location, rotation):
        camera.set_actor_location_and_rotation(location, rotation, False, True)

    def camera_rotation(self, world):
        """The rotation the first player sees through: (pitch, yaw), or None."""
        controller = _get(self.ue, "GameplayStatics").get_player_controller(world, 0)
        manager = getattr(controller, "player_camera_manager", None)
        if manager is None:
            return None
        r = manager.get_camera_rotation()
        return float(r.pitch), float(r.yaw)

    def camera_location(self, world):
        """Where the first player sees from: (x, y, z) in cm, or None."""
        controller = _get(self.ue, "GameplayStatics").get_player_controller(world, 0)
        manager = getattr(controller, "player_camera_manager", None)
        if manager is None or not hasattr(manager, "get_camera_location"):
            return None
        v = manager.get_camera_location()
        return float(v.x), float(v.y), float(v.z)

    def set_sun(self, world, elevation, yaw):
        cls = _get(self.ue, "DirectionalLight")
        lights = list(_get(self.ue, "GameplayStatics").get_all_actors_of_class(world, cls))
        for light in lights:
            light.set_actor_rotation(self.rotator(-elevation, yaw), False)
        return len(lights)

    def pause(self, world, paused):
        """The game paused (its time, and the time of the materials, stand still); slomo where
        the pause cannot be set."""
        statics = _get(self.ue, "GameplayStatics")
        setter = getattr(statics, "set_game_paused", None)
        if setter is not None:
            setter(world, paused)
            return "pause"
        self.console(world, "slomo 0.0001" if paused else "slomo 1")
        return "slomo"

    # ---- screenshots ----------------------------------------------------------------------------

    def screenshot_dir(self):
        paths = _get(self.ue, "Paths")
        return Path(paths.convert_relative_path_to_full(paths.screen_shot_dir()))

    def take_shot(self, world, name, width, height):
        """Asks for a screenshot of the view named name; the file appears a few frames later in
        screenshot_dir (find_shot)."""
        if getattr(self.ue, "SystemLibrary", None) is not None:
            self.console(world, f"HighResShot {width}x{height} filename={name}")
            return "HighResShot"
        automation = getattr(self.ue, "AutomationLibrary", None)
        _get(automation, "take_high_res_screenshot", "HighResShot or "
             "AutomationLibrary.take_high_res_screenshot")(width, height, name)
        return "AutomationLibrary"

    def saved_dir(self):
        paths = _get(self.ue, "Paths")
        saved = getattr(paths, "project_saved_dir", None)
        return Path(paths.convert_relative_path_to_full(saved())) if saved else None

    def find_shot(self, name, since):
        """The newest PNG named name* written after since (seconds of the epoch), in the folder of
        the screenshots, else anywhere in Saved; or None."""
        for folder in (self.screenshot_dir(), self.saved_dir()):
            found = []
            if folder is not None and folder.exists():
                for path in folder.rglob(f"{name}*.png"):
                    stat = path.stat()
                    if stat.st_mtime >= since and stat.st_size > 0:
                        found.append((stat.st_mtime, path))
            if found:
                return max(found)[1]
        return None

    # ---- the pawn -------------------------------------------------------------------------------

    def pawn(self, world):
        return _get(self.ue, "GameplayStatics").get_player_pawn(world, 0)

    @staticmethod
    def same_actor(a, b):
        """The same actor of the world (two wrappers of Python may hold one object)."""
        if a is b:
            return True
        if a is None or b is None:
            return False
        try:
            return a.get_path_name() == b.get_path_name()
        except Exception:  # noqa: BLE001 - no path name: identity only
            return False

    def controller(self, world):
        return _get(self.ue, "GameplayStatics").get_player_controller(world, 0)

    @staticmethod
    def location(actor):
        v = actor.get_actor_location()
        return float(v.x), float(v.y), float(v.z)

    @staticmethod
    def half_height(pawn):
        capsule = getattr(pawn, "capsule_component", None)
        return float(capsule.get_scaled_capsule_half_height()) if capsule is not None else 0.0

    @staticmethod
    def movement_mode(pawn):
        movement = getattr(pawn, "character_movement", None)
        if movement is None:
            return "none"
        name = str(movement.movement_mode).split(".")[-1].lower()
        return name[5:] if name.startswith("move_") else name

    def walk_input(self, pawn, dx, dy):
        pawn.add_movement_input(self.vector(dx, dy, 0.0), 1.0, True)

    def face(self, world, yaw):
        self.controller(world).set_control_rotation(self.rotator(0.0, yaw))

    def trace_down(self, world, x, y, z, ignore, depth=20000.0):
        """The height of what a trace straight down from (x, y, z) hits, or None."""
        system = _get(self.ue, "SystemLibrary")
        start, end = self.vector(x, y, z + 200.0), self.vector(x, y, z - depth)
        channel = self.ue.TraceTypeQuery.TRACE_TYPE_QUERY1
        hit = system.line_trace_single(world, start, end, channel, False, [ignore],
                                       self.ue.DrawDebugTrace.NONE, True)
        if hit is None:
            return None
        point = getattr(hit, "impact_point", None)
        if point is None and hasattr(hit, "to_tuple"):
            fields = hit.to_tuple()
            if not fields[0]:
                return None
            point = fields[5]
        return None if point is None else float(point.z)

    # ---- the end ---------------------------------------------------------------------------------

    def tick(self, callback):
        return _get(self.ue, "register_slate_post_tick_callback")(callback)

    def untick(self, handle):
        remove = getattr(self.ue, "unregister_slate_post_tick_callback", None)
        if remove is not None and handle is not None:
            remove(handle)

    def quit(self):
        _get(self.ue, "SystemLibrary").quit_editor()

    def log(self, text):
        getattr(self.ue, "log", print)(f"[terrain verify] {text}")


def job_path():
    return os.environ.get("TERRAIN_VERIFY_JOB")
