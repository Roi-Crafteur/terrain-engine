"""The driver of a session (ue_driver.py) with the dev tools of agent 36 (Terrain.Goto, Look,
View, Pawn) and the loads of agent 38 against the stand-in module unreal (fake_unreal.py): one
play per need of the planet file, the load each view waits for, the pawn of the game given back
before a walk or refused over water; and the heartbeat Windows refuses to replace while the
launcher reads it. The places and the helpers come from test_driver.py."""

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
from test_driver import BASIN, EDGE, LIP, calls, drive, job_in  # noqa: E402


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
            # The check at lisiere, the dry place, in the second play (essai 5: it was made at the
            # basin, the first place of the first play); the view only turned for it.
            self.assertEqual(r["pitchCheck"]["place"], "lisiere")
            self.assertIn("Terrain.Look 300.0 -30.0", commands)  # the check, down
            self.assertIn("Terrain.Look 300.0 30.0", commands)
            self.assertEqual(len([c for c in commands if c.startswith("Terrain.Goto")]), 3)
            self.assertIn("Terrain.Look 46.0 -20.0", commands)   # negative: down
            self.assertIn("Terrain.View normal", commands)
            self.assertEqual([v["loaded"] for v in r["views"]], ["reported"] * 3)
            self.assertFalse(calls(fake, "editor_spawn"))  # no camera of its own
            planet_files = [c[2] for c in fake.calls
                            if c[0] == "set_editor_property" and c[1] == "planet_file"]
            self.assertEqual(planet_files[:2], ["C:/TerrainVerify/cache/x/planet.bin", ""])

    def test_a_load_still_running_at_the_next_goto(self):
        """#58: a teleport first reports the load still running as it stands (here the one of
        Terrain.Pawn before the walk), then starts its own: the next view waits for its own."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=dict(self.FEATURES, loadStart=True))
            fake.pawn_load_seconds = 1000.0  # still running at the next Terrain.Goto
            walk = {"place": BASIN["name"], "heading": 90.0, "distance": 5.0, "yaw": 0.0}
            r = drive(fake, job_in(folder, fake, [BASIN, LIP], materials=False,
                                   pitchCheck=False, walks=[walk])).result
            self.assertTrue(r["done"], r["errors"])
            log = fake.log_path.read_text(encoding="utf-8")
            # Load 1 the spawn of the play, 2 the Terrain.Goto of BASIN, 3 Terrain.Pawn.
            stale = log.index("load 3 report")  # the load of Terrain.Pawn, as it stands
            self.assertLess(stale, log.index("load 4 (teleport) started at (-6185411.0"))
            self.assertEqual([(v["load"]["n"], v["load"]["reason"]) for v in r["views"]],
                             [(2, "teleport"), (4, "teleport")])
            self.assertEqual((r["views"][1]["load"]["x"], r["views"][1]["load"]["z"]),
                             (LIP["x"], LIP["z"]))
            # The first shot of LIP (after the shot and its repeat at BASIN) once load 4 reported.
            done = len(log[:log.index("load 4 report")].encode("utf-8"))
            self.assertGreater(fake.shot_log_sizes[2], done)

    def test_the_load_of_the_goto_written_late(self):
        """Essai 5: the spawn load of the play reported, the lines of Terrain.Goto in the file a
        little later (a thread writes the log): the view took the spawn load (n 1). It waits
        for the load of its Terrain.Goto, a teleport."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=dict(self.FEATURES, loadStart=True))
            fake.spawn_load_seconds, fake.goto_log_lag = 0.0, 0.5
            r = drive(fake, job_in(folder, fake, [BASIN, EDGE], materials=False,
                                   pitchCheck=False)).result
            self.assertTrue(r["done"], r["errors"])
            self.assertEqual([(v["load"]["n"], v["load"]["reason"]) for v in r["views"]],
                             [(2, "teleport"), (2, "teleport")])  # one play each

    def test_the_walk_waits_for_the_pawn_to_stand(self):
        """#57: Terrain.Pawn puts the pawn of the game down and ignores its move input until it
        stands (1 s a spot, up to 8 spots); the walk forces its input: it waits for that line."""
        with tempfile.TemporaryDirectory() as folder:
            features = dict(self.FEATURES, loadStart=True, pawnLanding=True)
            fake = fake_unreal.FakeUnreal(folder, features=features)
            fake.landing_seconds = 4.0  # longer than the former fixed wait of 2 s
            walk = {"place": BASIN["name"], "heading": 90.0, "distance": 5.0, "yaw": 0.0}
            r = drive(fake, job_in(folder, fake, [BASIN], materials=False, pitchCheck=False,
                                   walks=[walk])).result
            self.assertTrue(r["done"], r["errors"])
            self.assertEqual(fake.inputs_while_landing, 0)  # 11 with the former wait of 2 s
            (entry,) = r["walks"]
            self.assertEqual((entry["reason"], entry["landing"]["state"]), ("distance", "stands"))

    def test_a_walk_refused_by_terrain_pawn(self):
        """Over water Terrain.Pawn refuses and the flying pawn of the tools stays: no walk of it
        (it never falls), the reason kept."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=dict(self.FEATURES, pawnLanding=True))
            fake.pawn_refusal = ("over water (sea, lake or river): the pawn of the game is not "
                                 "put down here, the view of the tools stays")
            walk = {"place": LIP["name"], "heading": 46.0, "distance": 80.0, "yaw": -44.0}
            r = drive(fake, job_in(folder, fake, [LIP], materials=False, pitchCheck=False,
                                   walks=[walk], landTimeout=60.0)).result
            self.assertTrue(r["done"], r["errors"])
            (entry,) = r["walks"]
            self.assertEqual((entry["reason"], entry["file"]), ("pawn refused", None))
            self.assertIn("over water", entry["landing"]["message"])
            self.assertLess(fake.time, 60.0)  # the refusal read, not the whole wait

    def test_without_load_report_the_delays(self):
        with tempfile.TemporaryDirectory() as folder:
            features = dict(self.FEATURES, loadReport=False)
            fake = fake_unreal.FakeUnreal(folder, features=features)
            r = drive(fake, job_in(folder, fake, [BASIN], materials=False)).result
            self.assertEqual(r["views"][0]["loaded"], "delay")


class Heartbeat(unittest.TestCase):
    """Essai 5 (main): PermissionError [WinError 5] on heartbeat_1.tmp -> heartbeat_1.json, the
    launcher reading the file at that moment. The replace is tried again; a beat that still
    fails is skipped, never an error of the driver."""

    def driver(self, folder):
        fake = fake_unreal.FakeUnreal(folder)
        job = job_in(folder, fake, [BASIN], materials=False)
        return ue_driver.Driver(job, ue_api.Api(fake), clock=fake.clock, wall=fake.wall), job

    def test_a_file_locked_for_a_moment(self):
        with tempfile.TemporaryDirectory() as folder:
            driver, job = self.driver(folder)
            real, refusals = ue_driver.os.replace, [2]

            def locked(src, dst):
                if refusals[0] > 0:
                    refusals[0] -= 1
                    raise PermissionError(5, "Access is denied")
                real(src, dst)

            ue_driver.os.replace = locked
            try:
                driver.beat(force=True)
            finally:
                ue_driver.os.replace = real
            self.assertEqual(json.loads(Path(job["heartbeat"]).read_text())["n"], 1)
            self.assertNotIn("beatsSkipped", driver.result)

    def test_a_file_locked_for_good(self):
        with tempfile.TemporaryDirectory() as folder:
            driver, job = self.driver(folder)
            real = ue_driver.os.replace

            def locked(src, dst):
                raise PermissionError(5, "Access is denied")

            ue_driver.os.replace = locked
            try:
                driver.beat(force=True)  # no exception
            finally:
                ue_driver.os.replace = real
            self.assertEqual(driver.result["beatsSkipped"], 1)
            self.assertEqual(driver.result["errors"], [])
            driver.beat(force=True)  # the next one comes
            self.assertEqual(json.loads(Path(job["heartbeat"]).read_text())["n"], 2)

    def test_the_file_open_in_the_launcher(self):
        """On Windows a file open for reading cannot be replaced: the real case."""
        with tempfile.TemporaryDirectory() as folder:
            driver, job = self.driver(folder)
            driver.beat(force=True)
            with open(job["heartbeat"], encoding="utf-8"):  # the launcher reading it
                driver.beat(force=True)
            self.assertEqual(driver.result["errors"], [])
            skipped = driver.result.get("beatsSkipped", 0)
            self.assertEqual(skipped, 1 if ue_driver.os.name == "nt" else 0)


if __name__ == "__main__":
    unittest.main()
