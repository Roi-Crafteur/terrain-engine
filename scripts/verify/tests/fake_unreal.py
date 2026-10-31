"""A stand-in for the module unreal of the editor, for the tests of the driver: a level with a
Terrain World, a Play In Editor that starts a few ticks after it is asked, a pawn that walks at
5 m/s and falls beyond a distance, a camera manager, screenshots written as PNG files (sky when
the view looks up, ground when it looks down), the load reports of the plugin in the log. It
records every call; it never saves anything (a test checks no call is named save)."""

import math
import sys
import types
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pngio  # noqa: E402


class Vector:
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x, self.y, self.z = float(x), float(y), float(z)


class Rotator:
    def __init__(self, roll=0.0, pitch=0.0, yaw=0.0):
        self.roll, self.pitch, self.yaw = float(roll), float(pitch), float(yaw)


class Transform:
    def __init__(self, location=None, rotation=None, scale=None):
        self.location, self.rotation, self.scale = location, rotation, scale


class Actor:
    def __init__(self, fake, location=None, rotation=None):
        self.fake = fake
        self.location = location or Vector()
        self.rotation = rotation or Rotator()
        self.properties = {}

    def get_editor_property(self, name):
        return self.properties[name]

    def set_editor_property(self, name, value):
        self.fake.calls.append(("set_editor_property", name, value))
        self.properties[name] = value

    def get_actor_location(self):
        return self.location

    def set_actor_location_and_rotation(self, location, rotation, sweep, teleport):
        self.location, self.rotation = location, rotation

    def set_actor_rotation(self, rotation, teleport):
        self.rotation = rotation

    def set_actor_location(self, location, sweep, teleport):
        self.fake.calls.append(("teleport", location.x, location.y, location.z))
        self.location = location


class TerrainWorld(Actor):
    pass


class CameraActor(Actor):
    pass


class DirectionalLight(Actor):
    pass


class _Mode:
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return f"MovementMode.{self.name}"


class MovementMode:
    MOVE_WALKING = _Mode("MOVE_WALKING")
    MOVE_FALLING = _Mode("MOVE_FALLING")


class Pawn(Actor):
    def __init__(self, fake):
        super().__init__(fake, Vector(0.0, 0.0, 90.0))
        self.capsule_component = types.SimpleNamespace(
            get_scaled_capsule_half_height=lambda: 90.0)
        self.character_movement = types.SimpleNamespace(movement_mode=MovementMode.MOVE_WALKING)
        self.input = (0.0, 0.0)
        self.walked = 0.0

    def add_movement_input(self, direction, scale, force):
        self.input = (direction.x * scale, direction.y * scale)

    def advance(self, dt, fall_after):
        if self.character_movement.movement_mode is MovementMode.MOVE_WALKING:
            self.location.z = min(self.location.z, 90.0)  # landed on the ground (Z 0)
        if self.input == (0.0, 0.0):
            return
        step = 500.0 * dt
        self.location = Vector(self.location.x + self.input[0] * step,
                               self.location.y + self.input[1] * step, self.location.z)
        self.walked += step / 100.0
        if self.walked > fall_after:
            self.character_movement.movement_mode = MovementMode.MOVE_FALLING
            self.location.z -= 1000.0 * dt
        self.input = (0.0, 0.0)


class Controller:
    def __init__(self, fake):
        self.fake = fake
        self.view_target = None
        self.control = Rotator()
        self.player_camera_manager = types.SimpleNamespace(get_camera_rotation=self._rotation)

    def _rotation(self):
        target = self.view_target
        r = target.rotation if target is not None else self.control
        if self.fake.invert_pitch:
            return Rotator(pitch=-r.pitch, yaw=r.yaw)
        return Rotator(pitch=r.pitch, yaw=r.yaw)

    def set_view_target_with_blend(self, actor, blend):
        self.view_target = actor

    def set_control_rotation(self, rotation):
        self.control = rotation


class FakeUnreal:
    """The module: install as sys.modules["unreal"], or give it to ue_api.Api."""

    def __init__(self, folder, features=None, fall_after=1e9, invert_pitch=False,
                 shots_fail_for=()):
        self.folder = Path(folder)
        self.time = 0.0
        self.calls = []
        self.features = features or {}
        self.fall_after = fall_after
        self.invert_pitch = invert_pitch
        self.shots_fail_for = shots_fail_for
        self.terrain = TerrainWorld(self)
        self.terrain.properties.update(core_origin_x=0.0, core_origin_z=0.0,
                                       planet_file="Saved/TerrainEngine/planet_{Seed}.bin")
        self.editor_world = types.SimpleNamespace(name="editor")
        self.game = None
        self.starting_at = None
        self.loads = 0
        self.pending_shots = []
        self.log_path = self.folder / "Saved" / "Logs" / "TerrainSandbox.log"
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_path.write_text("LogInit: start\n", encoding="utf-8")
        self.pending_reports = []
        self._build_module()

    # ---- the clock of the tests ---------------------------------------------------------------

    def clock(self):
        return self.time

    def wall(self):
        return 1_700_000_000.0 + self.time

    def advance(self, dt):
        self.time += dt
        if self.starting_at is not None and self.time >= self.starting_at:
            terrain = TerrainWorld(self)
            terrain.properties["focus_actor"] = None
            self.game = types.SimpleNamespace(name="pie", pawn=Pawn(self),
                                              controller=Controller(self),
                                              light=DirectionalLight(self), terrain=terrain)
            self.starting_at = None
            self._report_later()
        if self.game is not None:
            self.game.pawn.advance(dt, self.fall_after)
        for at, name in list(self.pending_shots):
            if self.time >= at:
                self.pending_shots.remove((at, name))
                self._write_shot(name)
        for at in list(self.pending_reports):
            if self.time >= at:
                self.pending_reports.remove(at)
                self.loads += 1
                with open(self.log_path, "a", encoding="utf-8") as log:
                    log.write(f"LogTerrainEngine: Terrain World: load {self.loads} report\n")

    def _report_later(self):
        if self.features.get("loadReport"):
            self.pending_reports.append(self.time + 3.0)

    def _write_shot(self, name):
        target = self.game.controller.view_target if self.game else None
        pitch = (target.rotation.pitch if target is not None else
                 self.game.controller.control.pitch if self.game else 0.0)
        color = (120, 170, 230) if pitch > 0 else (90, 70, 50)
        image = pngio.blank(16, 9, color)
        if pitch <= 0:  # a textured ground
            for i in range(0, len(image.rgb), 3):
                image.rgb[i] = (image.rgb[i] + 7 * (i // 3)) % 256
        folder = self.folder / "Saved" / "Screenshots" / "WindowsEditor"
        folder.mkdir(parents=True, exist_ok=True)
        pngio.write_png(folder / f"{name}.png", image)

    # ---- the module ------------------------------------------------------------------------------

    def _build_module(self):
        fake = self
        self.TerrainWorld, self.CameraActor, self.DirectionalLight = (
            TerrainWorld, CameraActor, DirectionalLight)
        self.Vector, self.Rotator, self.Transform = Vector, Rotator, Transform
        self.MovementMode = MovementMode
        self.TraceTypeQuery = types.SimpleNamespace(TRACE_TYPE_QUERY1=1)
        self.DrawDebugTrace = types.SimpleNamespace(NONE=0)

        class UnrealEditorSubsystem:
            def get_editor_world(self):
                return fake.editor_world

            def get_game_world(self):
                return fake.game

        class LevelEditorSubsystem:
            def editor_request_begin_play(self):
                fake.calls.append(("begin_play",))
                fake.starting_at = fake.time + 2.0

            def editor_request_end_play(self):
                fake.calls.append(("end_play",))
                fake.game = None

            def is_in_play_in_editor(self):
                return fake.game is not None

        self.UnrealEditorSubsystem = UnrealEditorSubsystem
        self.LevelEditorSubsystem = LevelEditorSubsystem
        self._subsystems = {UnrealEditorSubsystem: UnrealEditorSubsystem(),
                            LevelEditorSubsystem: LevelEditorSubsystem()}
        self.GameplayStatics = _Statics(self)
        self.SystemLibrary = _System(self)
        self.Paths = types.SimpleNamespace(
            screen_shot_dir=lambda: str(fake.folder / "Saved" / "Screenshots"),
            project_saved_dir=lambda: str(fake.folder / "Saved"),
            convert_relative_path_to_full=lambda p: p)

    def get_editor_subsystem(self, cls):
        return self._subsystems[cls]

    def register_slate_post_tick_callback(self, callback):
        self.calls.append(("register_tick",))
        self.tick_callback = callback
        return "handle"

    def unregister_slate_post_tick_callback(self, handle):
        self.calls.append(("unregister_tick", handle))

    def log(self, text):
        self.calls.append(("log", text))


class _Statics:
    def __init__(self, fake):
        self.fake = fake

    def get_all_actors_of_class(self, world, cls):
        if world is self.fake.editor_world and cls is TerrainWorld:
            return [self.fake.terrain]
        if world is self.fake.game and cls is DirectionalLight:
            return [self.fake.game.light]
        if world is self.fake.game and cls is TerrainWorld:
            return [self.fake.game.terrain]
        return []

    def get_player_pawn(self, world, index):
        return world.pawn if world is not None else None

    def get_player_controller(self, world, index):
        return world.controller

    def begin_deferred_actor_spawn_from_class(self, world, cls, transform):
        self.fake.calls.append(("spawn", cls.__name__))
        return cls(self.fake, transform.location, transform.rotation)

    def finish_spawning_actor(self, actor, transform):
        return actor

    def set_game_paused(self, world, paused):
        self.fake.calls.append(("paused", paused))


class _System:
    def __init__(self, fake):
        self.fake = fake

    def execute_console_command(self, world, command):
        fake = self.fake
        fake.calls.append(("console", command))
        if command.startswith("HighResShot"):
            name = command.split("filename=")[1]
            if not any(part in name for part in fake.shots_fail_for):
                fake.pending_shots.append((fake.time + 0.5, name))
        elif command.startswith("Terrain.Look") and fake.game is not None:
            _, heading, pitch = command.split()
            fake.game.controller.view_target = None
            fake.game.controller.control = Rotator(pitch=float(pitch),
                                                   yaw=float(heading) - 90.0)
        elif command.startswith("Terrain.Goto") and fake.game is not None:
            fake._report_later()

    def line_trace_single(self, world, start, end, channel, complex_, ignore, debug, self_):
        return types.SimpleNamespace(impact_point=Vector(start.x, start.y, 0.0))

    def quit_editor(self):
        self.fake.calls.append(("quit",))


def install(fake):
    sys.modules["unreal"] = fake
    return fake


def distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])
