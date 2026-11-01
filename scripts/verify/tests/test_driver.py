"""The driver of a session (ue_driver.py) against a stand-in module unreal (fake_unreal.py): the
places one after the other, the camera where the frame puts it and pitched with the sign of the
places (negative is down in Unreal), the check view of the pitch, the pause around the
screenshots, the material scripts and their errors, the walk stopped at the first fall, an API
that is missing, nothing ever saved, the heartbeat and the result. The dev commands of agent 36,
the loads of agent 38 and the heartbeat refused by Windows: test_dev_commands.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import fake_unreal  # noqa: E402
import ue_api  # noqa: E402
import ue_driver  # noqa: E402
import walks  # noqa: E402

BASIN = {"name": "chute388_bassin", "x": -6185104.0, "z": 155924.0, "heading": 227.0,
         "pitch": 30.0, "yaw": 137.0, "eye": 182.03, "eyeMode": "altitude", "usePlanet": True,
         "ground": 178.818, "waterLevel": 180.028, "eyeAltitude": 182.03}
LIP = {"name": "chute388_levre", "x": -6185411.0, "z": 156218.0, "heading": 46.0,
       "pitch": -20.0, "yaw": -44.0, "eye": 2.0, "eyeMode": "ground", "usePlanet": True,
       "ground": 567.781, "waterLevel": 568.991, "eyeAltitude": 570.991}
K2 = {"name": "k2_paroi", "x": 416102.0, "z": -11414891.0, "heading": 225.0, "pitch": 35.0,
      "yaw": 135.0, "eye": 25.0, "eyeMode": "ground", "usePlanet": False, "ground": 6674.37,
      "waterLevel": None, "eyeAltitude": 6699.37}
EDGE = {"name": "lisiere", "x": -26927500.0, "z": -270336.0, "heading": 300.0, "pitch": -8.0,
        "yaw": 210.0, "eye": 40.0, "eyeMode": "ground", "usePlanet": False, "ground": 264.408,
        "waterLevel": None, "eyeAltitude": 304.408}


def job_in(folder, fake, places, **changes):
    fake_unreal.install(fake)  # what the material scripts import
    folder = Path(folder)
    scripts = folder / "Scripts"
    scripts.mkdir()
    (scripts / "create_ok.py").write_text("import unreal\nunreal.log('material')\n",
                                          encoding="utf-8")
    (scripts / "create_bad.py").write_text("raise ValueError('no such node')\n",
                                           encoding="utf-8")
    job = {"label": "avant", "sha": "abc", "out": str(folder / "out"),
           "heartbeat": str(folder / "heartbeat.json"), "result": str(folder / "result.json"),
           "scripts": str(scripts), "materials": True,
           "materialScripts": ["create_ok.py", "create_bad.py"], "editorLog": str(fake.log_path),
           "features": fake.features, "planetFile": "C:/TerrainVerify/cache/x/planet.bin",
           "places": places, "walks": [], "resolution": [16, 9], "startWait": 1.0,
           "shaderWait": 1.0, "settle": 2.0, "firstSettle": 3.0, "freezeSettle": 0.5,
           "loadTimeout": 30.0, "pawnTimeout": 30.0, "shotTimeout": 10.0, "sun": None}
    job.update(changes)
    return job


def drive(fake, job, seconds=600.0, dt=0.1):
    driver = ue_driver.Driver(job, ue_api.Api(fake), clock=fake.clock, wall=fake.wall)
    while not driver.done and fake.time < seconds:
        fake.advance(dt)
        driver.tick(dt)
    return driver


def calls(fake, kind):
    return [c for c in fake.calls if c[0] == kind]


class CameraSessions(unittest.TestCase):
    def test_two_places_with_the_camera(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            job = job_in(folder, fake, [BASIN, LIP])
            driver = drive(fake, job)
            r = driver.result
            self.assertTrue(r["done"], r["errors"])
            self.assertEqual(r["errors"], [])
            self.assertEqual(len(calls(fake, "begin_play")), 2)  # one play per place
            for view in r["views"]:
                self.assertTrue(Path(view["file"]).is_file(), view)
                self.assertTrue(Path(view["repeat"]).is_file(), view)
                self.assertEqual(view["loaded"], "delay")
                self.assertEqual(view["freeze"], "pause")
            self.assertEqual([m["ok"] for m in r["materials"]], [True, False])
            self.assertIn("no such node", r["materials"][1]["error"])
            # The Terrain World put back as it was, and nothing saved.
            self.assertEqual(fake.terrain.properties["core_origin_x"], 0.0)
            self.assertEqual(fake.terrain.properties["planet_file"],
                             "Saved/TerrainEngine/planet_{Seed}.bin")
            self.assertFalse([c for c in fake.calls if "save" in str(c[0]).lower()])
            beat = json.loads(Path(job["heartbeat"]).read_text())
            self.assertTrue(beat["done"])
            self.assertTrue(json.loads(Path(job["result"]).read_text())["done"])

    def test_the_sign_of_the_pitch(self):
        """A negative pitch stays negative in Unreal (looking down), a positive one up."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            job = job_in(folder, fake, [LIP], materials=False, pitchCheck=False)
            drive(fake, job)
            (spawn,) = calls(fake, "editor_spawn")
            _, cls, location, rotation = spawn
            self.assertEqual(cls, "CameraActor")
            self.assertEqual((rotation.pitch, rotation.yaw), (-20.0, -44.0))  # yaw: heading - 90
            self.assertEqual((location.x, location.y), (0.0, 0.0))
            self.assertAlmostEqual(location.z, 100.0 * (570.991 - 567.781))

    def test_the_camera_of_the_level(self):
        """UE 5.8 has no deferred spawn in Python: the camera is put in the level before the
        play, found again in it by its tag, the tiles around it; removed after each place, the
        Terrain World given its focus back, nothing saved."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            self.assertFalse(hasattr(fake.GameplayStatics, "begin_deferred_actor_spawn_from_class"))
            focus_in_play = []
            original = fake._start_play

            def start():
                original()
                focus_in_play.append(fake.game.terrain.properties["focus_actor"])
            fake._start_play = start
            r = drive(fake, job_in(folder, fake, [BASIN, LIP], materials=False)).result
            self.assertEqual(r["errors"], [])
            self.assertTrue(all(Path(v["file"]).is_file() for v in r["views"]))
            self.assertEqual([type(f).__name__ for f in focus_in_play], ["CameraActor"] * 2)
            self.assertEqual(len(calls(fake, "editor_spawn")), 2)
            self.assertEqual(len(calls(fake, "destroy")), 2)
            self.assertEqual(fake.editor_actors, [])
            self.assertIsNone(fake.terrain.properties["focus_actor"])
            self.assertEqual(r["missingApi"], [])

    def test_the_pitch_check(self):
        """Up then down by 30 degrees at the same place, the first dry one of the session that
        looks level or down (the foam of a basin looked down at fools one image alone; a wall
        looked up at shows no sky at +30 degrees)."""
        import pixels
        import pngio
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            r = drive(fake, job_in(folder, fake, [BASIN, LIP, K2, EDGE], materials=False)).result
            check = r["pitchCheck"]
            self.assertEqual(check["place"], "lisiere")  # over water, over water, pitched up
            self.assertEqual((check["requested"], check["seen"]), (-30.0, -30.0))
            self.assertEqual((check["upRequested"], check["upSeen"]), (30.0, 30.0))
            down, up = (pngio.read_png(check[k], use_pillow=False) for k in ("file", "upFile"))
            self.assertLess(pixels.sky_share(down) + pixels.SKY_MARGIN, pixels.sky_share(up))
            self.assertTrue(all(Path(v["file"]).is_file() for v in r["views"]))
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            r = drive(fake, job_in(folder, fake, [BASIN, LIP, K2], materials=False)).result
            self.assertTrue(r["done"], r["errors"])
            self.assertEqual(r["pitchCheck"], {"place": None,  # skipped, with a note
                                               "skipped": "no dry place looking level or down"})
            self.assertFalse([c for c in fake.calls if c[0] == "console" and "pitch" in c[1]])
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, invert_pitch=True)
            r = drive(fake, job_in(folder, fake, [EDGE], materials=False)).result
            self.assertEqual(r["pitchCheck"]["seen"], 30.0)  # what the launcher flags
            self.assertEqual(r["pitchCheck"]["upSeen"], -30.0)

    def test_paused_around_the_screenshots(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            drive(fake, job_in(folder, fake, [BASIN], materials=False, pitchCheck=False))
            order = [c for c in fake.calls
                     if c[0] == "paused" or (c[0] == "console" and "HighResShot" in c[1])]
            self.assertEqual([c[0] if c[0] == "paused" else "shot" for c in order],
                             ["paused", "shot", "shot", "paused"])
            self.assertEqual([c[1] for c in order if c[0] == "paused"], [True, False])

    def test_a_missing_screenshot_and_the_next_place(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, shots_fail_for=("chute388_bassin",))
            r = drive(fake, job_in(folder, fake, [BASIN, LIP], materials=False,
                                   pitchCheck=False)).result
            self.assertIsNone(r["views"][0]["file"])
            self.assertTrue(Path(r["views"][1]["file"]).is_file())
            self.assertTrue(r["done"])


class EditorReady(unittest.TestCase):
    def test_the_terrain_world_after_several_ticks(self):
        """init_unreal.py starts the driver before the map is open: it waits (phase
        editor-ready), then a few ticks, before the material scripts and the play."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, terrain_after=12.0)
            fake_unreal.install(fake)
            job = job_in(folder, fake, [BASIN], pitchCheck=False)
            driver = ue_driver.Driver(job, ue_api.Api(fake), clock=fake.clock, wall=fake.wall)
            phases = set()
            while not driver.done and fake.time < 600.0:
                fake.advance(0.1)
                driver.tick(0.1)
                if fake.time < 12.0:
                    phases.add(json.loads(Path(job["heartbeat"]).read_text())["phase"])
            self.assertEqual(phases, {"editor-ready"})
            self.assertTrue(driver.result["done"], driver.result["errors"])
            material = [c for c in fake.calls if c[0] == "log" and c[1] == "material"]
            self.assertGreaterEqual(material[0][2], 12.0 + 3.0)  # found, then readyWait
            begin = fake.calls.index(("begin_play",))
            self.assertLess(fake.calls.index(material[0]), begin)
            self.assertGreaterEqual(driver.result["editorReady"], 15.0)

    def test_started_by_init_unreal(self):
        import start_driver
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.install(fake_unreal.FakeUnreal(folder))
            job = job_in(folder, fake, [BASIN], materials=False)
            job_path = Path(folder) / "job.json"
            job_path.write_text(json.dumps(job), encoding="utf-8")
            init = Path(folder) / "Content" / "Python" / "init_unreal.py"
            init.parent.mkdir(parents=True)
            (init.parent / "init_unreal_project.py").write_text(
                f"open({str(Path(folder) / 'project_ran')!r}, 'w').write('x')\n")
            driver_file = Path(ue_driver.__file__)
            self.assertIsNone(start_driver.start({}, init))  # not started by the launcher
            self.assertFalse(calls(fake, "register_tick"))
            self.assertTrue((Path(folder) / "project_ran").exists())  # the project's, first
            env = {"TERRAIN_VERIFY_JOB": str(job_path), "TERRAIN_VERIFY_DRIVER": str(driver_file)}
            import os
            os.environ["TERRAIN_VERIFY_JOB"] = str(job_path)
            try:
                driver = start_driver.start(env, init)
            finally:
                del os.environ["TERRAIN_VERIFY_JOB"]
            self.assertIsNotNone(driver)
            self.assertTrue(calls(fake, "register_tick"))
            self.assertFalse(calls(fake, "quit"))  # open until the driver ends


class Reentrance(unittest.TestCase):
    """888435b: the rebuild of M_TemporaryTerrain ticked the editor inside the material script;
    the tick of the driver ran again inside its step (ValueError: generator already executing)
    and quit the editor from there, which froze it."""

    def scripts(self, folder):
        scripts = Path(folder) / "Scripts"
        (scripts / "create_rebuild.py").write_text("import unreal\nunreal.rebuild_material()\n",
                                                   encoding="utf-8")

    def test_a_tick_during_a_material_script(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            job = job_in(folder, fake, [BASIN], pitchCheck=False,
                         materialScripts=["create_rebuild.py", "create_ok.py"])
            self.scripts(folder)
            driver = ue_driver.Driver(job, ue_api.Api(fake), clock=fake.clock, wall=fake.wall)
            fake.reentrant = driver.tick
            while not driver.done and fake.time < 600.0:
                fake.advance(0.1)
                driver.tick(0.1)
            r = driver.result
            self.assertEqual(r["errors"], [])
            self.assertEqual([m["ok"] for m in r["materials"]], [True, True])
            self.assertGreaterEqual(r["reentered"], 5)
            self.assertTrue(Path(r["views"][0]["file"]).is_file())

    def test_quit_on_a_later_tick_outside_any_step(self):
        import os
        for scripts in (["create_rebuild.py", "create_ok.py"], ["create_rebuild.py"]):
            with self.subTest(scripts=scripts), tempfile.TemporaryDirectory() as folder:
                fake = fake_unreal.FakeUnreal(folder)
                # A level without its Terrain World: the session ends on an error, too.
                job = job_in(folder, fake, [BASIN], pitchCheck=False, materialScripts=scripts,
                             levelTimeout=5.0 if len(scripts) == 1 else 600.0)
                if len(scripts) == 1:
                    fake.terrain_after = 1e9
                self.scripts(folder)
                path = Path(folder) / "job.json"
                path.write_text(json.dumps(job), encoding="utf-8")
                os.environ["TERRAIN_VERIFY_JOB"] = str(path)
                try:
                    driver = ue_driver.main(fake)
                finally:
                    del os.environ["TERRAIN_VERIFY_JOB"]
                driver.clock, driver.wall, driver._since = fake.clock, fake.wall, fake.time
                seen = {}
                fake.on_quit = lambda: seen.update(in_step=driver._in_step, done=driver.done,
                                                   ticks=driver.ticks)
                done_at = None
                while not calls(fake, "quit") and fake.time < 600.0:
                    fake.advance(0.1)
                    fake.tick_callback(0.1)
                    if driver.done and done_at is None:
                        done_at = driver.ticks
                self.assertEqual(len(calls(fake, "quit")), 1)
                self.assertEqual((seen["in_step"], seen["done"]), (False, True))
                self.assertTrue(json.loads(Path(job["result"]).read_text())["done"])
                for _ in range(5):  # later ticks never quit again
                    fake.tick_callback(0.1)
                self.assertEqual(len(calls(fake, "quit")), 1)


class Walks(unittest.TestCase):
    def test_stopped_at_the_first_fall(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, fall_after=12.0)
            walk = {"place": "chute388_bassin", "heading": 90.0, "distance": 60.0, "yaw": 0.0}
            r = drive(fake, job_in(folder, fake, [BASIN], materials=False, pitchCheck=False,
                                   walks=[walk])).result
            (entry,) = r["walks"]
            self.assertEqual(entry["reason"], "fall")
            samples = walks.read_walk(entry["file"])
            self.assertGreater(samples[-1].distance, 12.0)
            self.assertLess(samples[-1].distance, 20.0)
            # East along the heading of 90 degrees: x grows, z stays.
            self.assertGreater(samples[-1].x, BASIN["x"] + 10.0)
            self.assertAlmostEqual(samples[-1].z, BASIN["z"], places=3)
            self.assertAlmostEqual(samples[0].feet, BASIN["ground"], places=3)
            self.assertEqual(samples[-1].mode, "falling")
            # The pawn put on the ground of the place before it walks, the focus back on it.
            self.assertEqual(calls(fake, "teleport")[0][1:], (0.0, 0.0, 140.0))


class Failures(unittest.TestCase):
    def test_a_level_without_terrain_world(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            fake.GameplayStatics.get_all_actors_of_class = lambda world, cls: []
            r = drive(fake, job_in(folder, fake, [BASIN], materials=False,
                                   levelTimeout=5.0)).result
            self.assertTrue(r["done"])
            self.assertIn("no Terrain World", r["errors"][0])
            self.assertFalse(calls(fake, "begin_play"))

    def test_every_missing_function_at_once(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            del fake.EditorActorSubsystem
            fake.GameplayStatics.get_all_actors_with_tag = None
            walk = {"place": "chute388_bassin", "heading": 90.0, "distance": 60.0, "yaw": 0.0}
            r = drive(fake, job_in(folder, fake, [BASIN], materials=False, walks=[walk])).result
            self.assertTrue(r["done"])
            # Both named together (Actor.destroy_actor stands in for the subsystem).
            self.assertEqual(r["missingApi"], [
                "EditorActorSubsystem.spawn_actor_from_class|EditorLevelLibrary"
                ".spawn_actor_from_class",
                "GameplayStatics.get_all_actors_with_tag"])
            self.assertFalse(calls(fake, "begin_play"))

    def test_the_walks_skipped_without_their_functions(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            fake.Pawn = type("PawnWithoutInput", (), {})
            walk = {"place": "chute388_bassin", "heading": 90.0, "distance": 60.0, "yaw": 0.0}
            r = drive(fake, job_in(folder, fake, [BASIN], materials=False, pitchCheck=False,
                                   walks=[walk])).result
            self.assertEqual(r["missingApi"], ["Pawn.add_movement_input"])
            self.assertTrue(Path(r["views"][0]["file"]).is_file())  # the views still taken
            self.assertEqual(r["walks"][0]["reason"], "API missing")

    def test_a_missing_api(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            del fake.LevelEditorSubsystem
            r = drive(fake, job_in(folder, fake, [BASIN], materials=False)).result
            self.assertTrue(r["done"])
            self.assertIn("LevelEditorSubsystem", " ".join(r["missingApi"]))

    def test_main_ticks_then_quits(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            job = job_in(folder, fake, [BASIN], materials=False, pitchCheck=False)
            path = Path(folder) / "job.json"
            path.write_text(json.dumps(job), encoding="utf-8")
            import os
            os.environ["TERRAIN_VERIFY_JOB"] = str(path)
            try:
                driver = ue_driver.main(fake)
            finally:
                del os.environ["TERRAIN_VERIFY_JOB"]
            driver.clock, driver.wall = fake.clock, fake.wall
            driver._since = fake.time
            while not driver.done and fake.time < 300:
                fake.advance(0.1)
                fake.tick_callback(0.1)
            self.assertFalse(calls(fake, "quit"))  # never in the tick where the driver ends
            fake.tick_callback(0.1)
            self.assertTrue(calls(fake, "quit"))
            self.assertTrue(calls(fake, "unregister_tick"))


if __name__ == "__main__":
    unittest.main()
