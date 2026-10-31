"""The driver of a session (ue_driver.py) against a stand-in module unreal (fake_unreal.py): the
places one after the other, the camera where the frame puts it and pitched with the sign of the
places (negative is down in Unreal), the check view of the pitch, the pause around the
screenshots, the material scripts and their errors, the walk stopped at the first fall, the dev
commands of agent 36 and the load reports of agent 38 when the version has them, an API that is
missing, nothing ever saved, the heartbeat and the result."""

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
EDGE = {"name": "lisiere", "x": -26927500.0, "z": -270336.0, "heading": 300.0, "pitch": -8.0,
        "yaw": 210.0, "eye": 40.0, "eyeMode": "ground", "usePlanet": False, "ground": 264.408,
        "waterLevel": None, "eyeAltitude": 304.408}


def job_in(folder, fake, places, **changes):
    folder = Path(folder)
    scripts = folder / "Scripts"
    scripts.mkdir()
    (scripts / "create_ok.py").write_text("RAN = True\n", encoding="utf-8")
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
            spawned = []
            original = fake.GameplayStatics.begin_deferred_actor_spawn_from_class

            def spawn(world, cls, transform):
                spawned.append(transform)
                return original(world, cls, transform)
            fake.GameplayStatics.begin_deferred_actor_spawn_from_class = spawn
            drive(fake, job)
            rotation = spawned[0].rotation
            self.assertEqual(rotation.pitch, -20.0)
            self.assertEqual(rotation.yaw, -44.0)  # heading 46 - 90
            location = spawned[0].location
            self.assertEqual((location.x, location.y), (0.0, 0.0))
            self.assertAlmostEqual(location.z, 100.0 * (570.991 - 567.781))
            # The tiles stream around the camera (the focus of the Terrain World of the play).
            focus = [c for c in fake.calls if c[0] == "set_editor_property" and
                     c[1] == "focus_actor"]
            self.assertEqual(type(focus[0][2]).__name__, "CameraActor")

    def test_the_pitch_check(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder)
            r = drive(fake, job_in(folder, fake, [BASIN, LIP], materials=False)).result
            check = r["pitchCheck"]
            self.assertEqual((check["place"], check["requested"], check["seen"]),
                             ("chute388_bassin", -30.0, -30.0))
            self.assertTrue(Path(check["file"]).is_file())
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, invert_pitch=True)
            r = drive(fake, job_in(folder, fake, [BASIN], materials=False)).result
            self.assertEqual(r["pitchCheck"]["seen"], 30.0)  # what the launcher flags

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


class DevCommands(unittest.TestCase):
    FEATURES = {"goto": True, "look": True, "viewMode": True, "pawn": True, "loadReport": True}

    def test_goto_look_and_the_load_reports(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=dict(self.FEATURES))
            r = drive(fake, job_in(folder, fake, [BASIN, LIP, EDGE], materials=False)).result
            self.assertTrue(r["done"], r["errors"])
            # One play per need of the planet file: with it (2 places), without it (1).
            self.assertEqual(len(calls(fake, "begin_play")), 2)
            commands = [c[1] for c in calls(fake, "console")]
            self.assertIn("Terrain.Goto -6185104.0 155924.0 182.03", commands)
            self.assertIn("Terrain.Goto -6185411.0 156218.0 +2.00", commands)
            self.assertIn("Terrain.Look 227.0 -30.0", commands)  # the check, down
            self.assertIn("Terrain.Look 227.0 30.0", commands)
            self.assertIn("Terrain.Look 46.0 -20.0", commands)   # negative: down
            self.assertIn("Terrain.View normal", commands)
            self.assertEqual([v["loaded"] for v in r["views"]], ["reported"] * 3)
            self.assertFalse(calls(fake, "spawn"))  # no camera of its own
            planet_files = [c[2] for c in fake.calls
                            if c[0] == "set_editor_property" and c[1] == "planet_file"]
            self.assertEqual(planet_files[:2], ["C:/TerrainVerify/cache/x/planet.bin", ""])

    def test_without_load_report_the_delays(self):
        with tempfile.TemporaryDirectory() as folder:
            features = dict(self.FEATURES, loadReport=False)
            fake = fake_unreal.FakeUnreal(folder, features=features)
            r = drive(fake, job_in(folder, fake, [BASIN], materials=False)).result
            self.assertEqual(r["views"][0]["loaded"], "delay")


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
            self.assertTrue(calls(fake, "quit"))
            self.assertTrue(calls(fake, "unregister_tick"))


if __name__ == "__main__":
    unittest.main()
