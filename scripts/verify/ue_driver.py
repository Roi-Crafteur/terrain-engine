"""The driver of a session of the verification, started in the Unreal editor by init_unreal.py
of the copy of the project (start_driver.py; the job in TERRAIN_VERIFY_JOB;
scripts/verify/README.md). It quits the editor itself at its end.

On the tick of the editor (register_slate_post_tick_callback), one step after the other:
0. the editor ready: the map open with its Terrain World, then a few ticks (phase editor-ready);
1. the material scripts of the version, run as the `py` command of the editor runs them;
2. for each place: CoreOriginX/Z and PlanetFile of the Terrain World of the level set (never
   saved, put back at the end), Play In Editor, the view placed (a CameraActor, or Terrain.Goto
   and Terrain.Look of the dev tools when the version has them), the load awaited ("Terrain
   World: load N report" when the version logs it, else a delay), the game paused (the time of
   the water stands still), the screenshot, a second one to measure what still moves;
3. the first view of the session is checked: pitched down by 30 degrees, what the camera says
   it looks at and the share of sky of its image (the launcher reads it);
4. the walks of the place: the character walks along a heading until the first fall (walks.py),
   its log written;
5. the end of the play, the result, then the editor closes.
A heartbeat (JSON, about every second) lets the launcher kill a session that hangs.
"""

import json
import sys
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import ue_api  # noqa: E402
import ue_log  # noqa: E402
from ue_walk import Until, Wait, WalkSteps  # noqa: E402

PITCH_CHECK = -30.0  # degrees: the check view looks down by this much
CAMERA_TAG = "TerrainVerifyCamera"  # the camera put in the level, found again in the play


class Driver(WalkSteps):
    def __init__(self, job, api, clock=time.monotonic, wall=time.time):
        self.job, self.api, self.clock, self.wall = job, api, clock, wall
        self.out = Path(job["out"])
        self.result = {"label": job["label"], "sha": job.get("sha"), "views": [], "walks": [],
                       "materials": [], "errors": [], "missingApi": [], "pitchCheck": None,
                       "features": job.get("features", {}), "done": False}
        self.phase, self.place, self.beats = "start", None, 0
        self.terrain, self.saved, self.origin = None, {}, None
        self.editor_camera, self.skip_walks, self.skip_sun = None, False, False
        self.done = False
        self.ticks, self.started, self._in_step = 0, clock(), False
        self._steps = self.steps()
        self._waiting, self._since, self._last_beat = None, clock(), -1e9

    # ---- the tick ------------------------------------------------------------------------------

    def tick(self, _delta=0.0):
        """One step when the current wait is over. A tick that comes while a step runs (the
        rebuild of a material makes the editor tick inside the material script) only writes the
        heartbeat: a step never starts inside another."""
        if self.done:
            return
        self.ticks += 1
        self.beat()
        if self._in_step:
            self.result["reentered"] = self.result.get("reentered", 0) + 1
            return
        self._in_step = True
        try:
            if self._waiting is not None and not self._ready():
                return
            self._waiting = next(self._steps)
            self._since = self.clock()
        except StopIteration:
            self.finish()
        except ue_api.MissingApi as missing:
            if not self.result["missingApi"]:
                self.result["missingApi"].append(str(missing))
            self.finish()
        except Exception:  # noqa: BLE001 - recorded, then the session ends cleanly
            self.result["errors"].append(traceback.format_exc())
            self.finish()
        finally:
            self._in_step = False

    def _ready(self):
        w = self._waiting
        elapsed = self.clock() - self._since
        if isinstance(w, Wait):
            return elapsed >= w.seconds
        if w.ready():
            return True
        if elapsed >= w.timeout:
            w.timed_out = True
            return True
        return False

    def beat(self, force=False):
        now = self.clock()
        if not force and now - self._last_beat < 1.0:
            return
        self._last_beat = now
        self.beats += 1
        state = {"phase": self.phase, "place": self.place, "n": self.beats, "time": self.wall(),
                 "done": self.result["done"]}
        path = Path(self.job["heartbeat"])
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(state), encoding="utf-8")
        tmp.replace(path)

    def save_result(self):
        Path(self.job["result"]).write_text(json.dumps(self.result, indent=2), encoding="utf-8")

    def set_phase(self, phase):
        self.phase = phase
        self.beat(force=True)

    # ---- the steps -------------------------------------------------------------------------------

    def steps(self):
        self.check_api()
        yield from self.editor_ready()
        if self.job.get("materials"):
            self.set_phase("materials")
            yield from self.materials()
        for group in self.groups():
            try:
                yield from self.play(group)
            except ue_api.MissingApi:
                raise
            except Exception:  # noqa: BLE001 - the next places still get their views
                self.result["errors"].append(traceback.format_exc())
                yield from self.stop_play()
            self.save_result()

    def check_api(self):
        """Every function the session needs, checked at once before anything: all those that
        are missing named together; the views need the editor (and the camera without the dev
        tools), the walks and the sun are only skipped without theirs."""
        needed = ["editor"] + ([] if self.dev_commands() else ["camera"])
        missing = self.api.missing_of(needed)
        walks_missing = self.api.missing_of(["walk"]) if self.job.get("walks") else []
        sun_missing = self.api.missing_of(["sun"]) if self.job.get("sun") else []
        self.result["missingApi"] = missing + walks_missing + sun_missing
        self.skip_walks, self.skip_sun = bool(walks_missing), bool(sun_missing)
        if missing:
            raise ue_api.MissingApi("unreal API missing: " + ", ".join(missing))

    def editor_ready(self):
        """init_unreal.py runs early, before the map is open and its assets ready: the world of
        the editor holding its Terrain World first, then a few more ticks and seconds."""
        self.set_phase("editor-ready")
        until = Until(self.find_terrain, self.job.get("levelTimeout", 600.0),
                      "the Terrain World of the level")
        yield until
        if until.timed_out:
            raise RuntimeError("no Terrain World in the level of the map after "
                               f"{self.job.get('levelTimeout', 600.0):.0f} s")
        found = self.ticks
        yield Wait(self.job.get("readyWait", 3.0))
        yield Until(lambda: self.ticks - found >= self.job.get("readyTicks", 30), 120.0,
                    "a few ticks of the editor")
        self.result["editorReady"] = round(self.clock() - self.started, 1)

    def find_terrain(self):
        """True once the level of the map holds its Terrain World (its settings then kept)."""
        if getattr(self.api.ue, "TerrainWorld", None) is None:
            return False  # the plugin not loaded yet: the timeout names it if it never is
        world = self.api.editor_world()
        worlds = self.api.terrain_worlds(world) if world is not None else []
        if not worlds:
            return False
        self.terrain = worlds[0]
        for name in ("core_origin_x", "core_origin_z", "planet_file", "focus_actor"):
            self.saved[name] = self.api.read(self.terrain, name)
        return True

    def materials(self):
        scripts = Path(self.job["scripts"])
        for name in self.job.get("materialScripts", []):
            yield Wait(0.0)  # each script in a step of its own, nothing else in it
            path = scripts / name
            started = self.clock()
            entry = {"script": name, "ok": True, "error": None}
            try:
                code = compile(path.read_text(encoding="utf-8"), str(path), "exec")
                exec(code, {"__name__": "__main__", "__file__": str(path)})  # noqa: S102
            except Exception:  # noqa: BLE001 - recorded for the summary
                entry.update(ok=False, error=traceback.format_exc())
            entry["seconds"] = round(self.clock() - started, 2)
            self.result["materials"].append(entry)
            yield Wait(0.5)
        yield Wait(self.job.get("shaderWait", 30.0))

    def groups(self):
        """One play per place, or with the dev tools one play per need of the planet file."""
        places = self.job["places"]
        if not self.dev_commands():
            return [[p] for p in places]
        with_planet = [p for p in places if p["usePlanet"]]
        without = [p for p in places if not p["usePlanet"]]
        return [g for g in (with_planet, without) if g]

    def dev_commands(self):
        f = self.result["features"]
        return bool(self.job.get("devCommands", True) and f.get("goto") and f.get("look"))

    def world(self):
        return self.api.game_world()

    def log_size(self):
        path = Path(self.job.get("editorLog", ""))
        return path.stat().st_size if path.is_file() else 0

    def load_reported(self, offset):
        path = Path(self.job.get("editorLog", ""))
        if not path.is_file():
            return False
        with open(path, "rb") as file:
            file.seek(offset)
            return bool(ue_log.load_reports(file.read().decode("utf-8", "replace")))

    def play(self, group):
        first = group[0]
        self.api.write(self.terrain, "core_origin_x", float(first["x"]))
        self.api.write(self.terrain, "core_origin_z", float(first["z"]))
        self.api.write(self.terrain, "planet_file",
                       self.job["planetFile"] if first["usePlanet"] else "")
        self.origin = (float(first["x"]), float(first["z"]), float(first["ground"]))
        if not self.dev_commands():
            self.put_camera(first)
        self.set_phase("play")
        offset = self.log_size()
        self.api.begin_play()
        until = Until(lambda: self.world() is not None and self.api.pawn(self.world()),
                      self.job.get("pawnTimeout", 180.0), "the play and its pawn")
        yield until
        if until.timed_out:
            raise RuntimeError("the play did not start or has no pawn")
        world = self.world()
        for place in group:
            self.place = place["name"]
            self.set_phase("place")
            yield from self.view(world, place, offset)
            offset = None
            yield from self.walks_of(world, place)
            self.save_result()
        yield from self.stop_play()

    def put_camera(self, place):
        """Without the dev tools: a CameraActor at the eye of the place in the level, before the
        play duplicates it; the Terrain World streams around it (its FocusActor, which the
        duplication points at the copy)."""
        self.remove_camera()
        self.editor_camera = self.api.spawn_editor_camera(
            self.eye_location(place), self.api.rotator(place["pitch"], place["yaw"]), CAMERA_TAG)
        self.api.write(self.terrain, "focus_actor", self.editor_camera)

    def remove_camera(self):
        if self.editor_camera is None:
            return
        try:
            self.api.destroy_editor_actor(self.editor_camera)
        except Exception:  # noqa: BLE001 - the level is never saved anyway
            self.result["errors"].append("could not remove the camera of the level")
        self.editor_camera = None
        self.api.write(self.terrain, "focus_actor", self.saved.get("focus_actor"))

    def stop_play(self):
        if self.api.playing():
            self.api.end_play()
            yield Until(lambda: not self.api.playing(), 60.0, "the end of the play")
        self.remove_camera()

    def eye_location(self, place):
        x0, z0, y0 = self.origin
        return self.api.vector(100.0 * (place["x"] - x0), 100.0 * (place["z"] - z0),
                               100.0 * (place["eyeAltitude"] - y0))

    # ---- a view ----------------------------------------------------------------------------------

    def aim(self, world, place, pitch, camera):
        """The view of the place pitched by pitch: the dev tools, or the camera of the place in
        the world of the play."""
        if self.dev_commands():
            eye = place["eye"]
            text = f"{eye:.2f}" if place["eyeMode"] == "altitude" else f"+{eye:.2f}"
            self.api.console(world, f"Terrain.Goto {place['x']} {place['z']} {text}")
            self.api.console(world, f"Terrain.Look {place['heading']} {pitch}")
            return
        self.api.place_camera(camera, self.eye_location(place),
                              self.api.rotator(pitch, place["yaw"]))

    def play_camera(self, world, place):
        """The copy of the camera of the level in the play, the view through it; None with the
        dev tools (Terrain.Goto, Terrain.Look)."""
        if self.dev_commands():
            self.aim(world, place, place["pitch"], None)
            return None
        camera = self.api.find_tagged(world, CAMERA_TAG)
        if camera is None:
            raise RuntimeError(f"the camera {CAMERA_TAG} of the level is not in the play")
        self.api.view_through(world, camera)
        return camera

    def view(self, world, place, offset):
        entry = {"place": place["name"], "file": None, "repeat": None, "loaded": None,
                 "freeze": None, "method": None}
        self.result["views"].append(entry)
        offset = self.log_size() if offset is None else offset
        camera = self.play_camera(world, place)
        if self.result["features"].get("loadReport"):
            until = Until(lambda: self.load_reported(offset), self.job.get("loadTimeout", 120.0),
                          "the load report")
            yield until
            entry["loaded"] = "timeout" if until.timed_out else "reported"
        else:
            yield Wait(self.job.get("firstSettle", 45.0) if len(self.result["views"]) == 1
                       else self.job.get("settle", 20.0))
            entry["loaded"] = "delay"
        if self.result["features"].get("viewMode"):
            self.api.console(world, f"Terrain.View {self.job.get('viewMode', 'normal')}")
        for command in self.job.get("console", []):
            self.api.console(world, command)
        sun = self.job.get("sun")
        if sun and not self.skip_sun:
            entry["suns"] = self.api.set_sun(world, sun["elevation"], sun["yaw"])
        yield Wait(self.job.get("settle", 20.0))
        if self.job.get("pitchCheck", True) and self.result["pitchCheck"] is None:
            yield from self.check_pitch(world, place, camera)
            self.aim(world, place, place["pitch"], camera)
            yield Wait(2.0)
        yield from self.shots(world, place, entry)

    def shots(self, world, place, entry):
        frozen = self.job.get("freeze", True)
        if frozen:
            entry["freeze"] = self.api.pause(world, True)
            yield Wait(self.job.get("freezeSettle", 1.5))
        width, height = self.job.get("resolution", [1600, 900])
        name = place["name"]
        entry["file"], entry["method"] = yield from self.shot(world, name, width, height, name)
        if self.job.get("repeatShot", True):
            yield Wait(2.0)
            entry["repeat"], _ = yield from self.shot(world, name + "_repeat", width, height,
                                                      name + "__repeat")
        if frozen:
            self.api.pause(world, False)

    def shot(self, world, name, width, height, stem):
        """The screenshot of the view, moved to <out>/views/<stem>.png: (path, method)."""
        asked = f"{self.job['label']}_{name}"
        since = self.wall() - 1.0
        method = self.api.take_shot(world, asked, width, height)
        found = {}

        def ready():
            found["path"] = self.api.find_shot(asked, since)
            return found["path"] is not None
        until = Until(ready, self.job.get("shotTimeout", 60.0), "the screenshot")
        yield until
        if until.timed_out:
            return None, method
        yield Wait(0.5)  # the file written whole
        target = self.out / "views" / f"{stem}.png"
        target.parent.mkdir(parents=True, exist_ok=True)
        found["path"].replace(target)
        return str(target), method

    def check_pitch(self, world, place, camera):
        """The first view of the session pitched down by 30 degrees: the pitch the camera says it
        has, and a small image whose share of sky the launcher measures."""
        self.aim(world, place, PITCH_CHECK, camera)
        yield Wait(2.0)
        seen = self.api.camera_rotation(world)
        check = {"place": place["name"], "requested": PITCH_CHECK,
                 "seen": None if seen is None else seen[0], "file": None}
        self.result["pitchCheck"] = check
        check["file"], _ = yield from self.shot(world, "pitch_check", 480, 270,
                                                "_pitch_check_" + place["name"])

    # ---- the end -------------------------------------------------------------------------------------

    def restore(self):
        self.remove_camera()
        for name, value in self.saved.items():
            try:
                self.api.write(self.terrain, name, value)
            except Exception:  # noqa: BLE001 - the level is never saved anyway
                self.result["errors"].append(f"could not put {name} back")

    def finish(self):
        if self.done:
            return
        self.done = True
        try:
            if self.api.playing():
                self.api.end_play()
        except Exception:  # noqa: BLE001 - quitting ends the play too
            pass
        if self.terrain is not None:
            self.restore()
        self.result["done"] = True
        self.save_result()
        self.phase = "done"
        self.beat(force=True)


def main(unreal=None):
    """Started by the editor: reads the job, ticks the driver, quits the editor at the end."""
    import importlib
    unreal = unreal or importlib.import_module("unreal")
    api = ue_api.Api(unreal)
    path = ue_api.job_path()
    if not path:
        api.log("no job (TERRAIN_VERIFY_JOB): nothing to do")
        return None
    job = json.loads(Path(path).read_text(encoding="utf-8"))
    driver = Driver(job, api)
    state = {}

    def on_tick(delta):
        # The editor is quit on a tick after the one where the driver ended, outside any step
        # (quit_editor from inside a step, itself inside a tick, froze the editor on 888435b).
        if state.get("quit"):
            if not state.get("quitting"):
                state["quitting"] = True
                api.untick(state.get("handle"))
                api.quit()
            return
        driver.tick(delta)
        if driver.done and not driver._in_step:
            if job.get("quit", True):
                state["quit"] = True
            else:
                api.untick(state.get("handle"))
    state["handle"] = api.tick(on_tick)
    return driver


if __name__ == "__main__":
    main()
