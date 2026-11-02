"""The command (verify.py): its arguments, a dry run from end to end that writes nothing, the
errors it names."""

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import machine  # noqa: E402
import verify  # noqa: E402

EXAMPLE = Path(__file__).resolve().parents[1] / "machine.example.json"
NOWHERE = "C:/TerrainVerifyNowhere" if os.name == "nt" else "/nonexistent/terrain-verify"


def machine_file(folder):
    data = json.loads(EXAMPLE.read_text(encoding="utf-8"))
    data["workRoot"] = NOWHERE
    data["repository"] = str(Path(folder) / "no-repository")  # ls-remote fails at once
    path = Path(folder) / "machine.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def run(*argv):
    """The exit code and the outputs of the command, the paths written with "/" (Windows writes
    them with "\\")."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = verify.main(list(argv))
    return code, out.getvalue().replace("\\", "/"), err.getvalue().replace("\\", "/")


class DryRun(unittest.TestCase):
    def test_every_step_printed_nothing_written(self):
        with tempfile.TemporaryDirectory() as folder:
            code, out, _ = run("run", "--machine", str(machine_file(folder)),
                               "--before", "c761ad2", "--after", "#57",
                               "--places", "chute388_bassin,lisiere_tropicale_savane",
                               "--sun", "falls", "--walk", "chute388_bassin:227:60",
                               "--dry-run")
            self.assertEqual(code, 0)
            for expected in ("avant : c761ad2", "apres : #57", "refs/pull/57/head",
                             "git ls-remote", "cache/c761ad2/", "cache/57/",
                             "TERRAIN_VERIFY_DRIVER=",
                             "git clone --no-hardlinks", "TerrainSandboxEditor Win64 Development",
                             "-DTERRAIN_REPO=", "--planet-check C:/TerrainEngine/planet.bin",
                             "--sections relief,water -6185104.0 155924.0",
                             "--sections relief,water -26927500.0 -270336.0",
                             "--line -6185104.0 155924.0",
                             "/Game/TerrainTest/TerrainGasp", "TERRAIN_VERIFY_JOB="):
                self.assertIn(expected, out)
            self.assertFalse(Path(NOWHERE).exists())
            self.assertNotIn("-ExecutePythonScript", out)

    def test_start_by_exec_cmds(self):
        with tempfile.TemporaryDirectory() as folder:
            _, out, _ = run("run", "--machine", str(machine_file(folder)), "--before", "main",
                            "--places", "chute388_bassin", "--start", "execcmds", "--dry-run")
            self.assertIn("start_driver.py", out)
            self.assertIn("-ExecCmds", out)

    def test_planet_no_probes_without_it(self):
        with tempfile.TemporaryDirectory() as folder:
            _, out, _ = run("run", "--machine", str(machine_file(folder)), "--before", "main",
                            "--places", "chute388_bassin", "--planet", "no", "--dry-run")
            self.assertIn("version : main", out)
            self.assertNotIn("--planet-check", out)
            self.assertNotIn("--planet ", out)


class WalkCores(unittest.TestCase):
    def test_the_core_under_every_sample(self):
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import fakes
        import walks
        import workspace

        class Probe(fakes.FakeRunner):
            def run(self, command, log=None, cwd=None, check=True):
                command = [str(c) for c in command]
                if "terrain_probe" in Path(command[0]).name:
                    self.commands.append(command)
                    coords = [float(c) for c in command[command.index("relief,water") + 1:]]
                    points = [{"relief": {"relief": x, "ground": x / 10.0},
                               "water": {"kind": "dry", "level": None}}
                              for x in coords[0::2]]
                    return 0, json.dumps({"points": points})
                return super().run(command, log, cwd, check)

        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            v = workspace.prepare(m, "main", "apres", fakes.FakeRunner())
            csv = Path(root) / "walk.csv"
            walks.write_walk(csv, [walks.Sample(0.1 * i, 10.0 * i, 0.0, 1.0, "walking", None, i)
                                   for i in range(5)])
            sessions = [{"result": {"walks": [{"place": "a", "file": str(csv)}]}}]
            jobs = [{"name": "a", "usePlanet": True}]
            runner = Probe()
            cores = verify.walk_cores(runner, v, m, sessions, jobs, v.project_dir / "p.bin",
                                      None, most=2)
            # 5 samples, at most 2 probed by call: every 3rd (x 0 and 30), held for the others.
            self.assertEqual(cores, {"a": [0.0, 0.0, 0.0, 3.0, 3.0]})
            self.assertIn("--planet", runner.commands[-1])


class Errors(unittest.TestCase):
    def test_an_unknown_place(self):
        with tempfile.TemporaryDirectory() as folder:
            code, _, err = run("run", "--machine", str(machine_file(folder)), "--before", "main",
                               "--places", "atlantide", "--dry-run")
            self.assertEqual(code, 2)
            self.assertIn("atlantide", err)

    def test_a_walk_of_a_place_not_asked(self):
        with tempfile.TemporaryDirectory() as folder, self.assertRaises(SystemExit):
            run("run", "--machine", str(machine_file(folder)), "--before", "main", "--places",
                "fleuve_plaine", "--walk", "k2_paroi", "--dry-run")

    def test_landings(self):
        with tempfile.TemporaryDirectory() as folder:
            code, out, _ = run("run", "--machine", str(machine_file(folder)), "--before", "main",
                               "--places", "k2_paroi,lisiere_tropicale_savane", "--landings",
                               "all", "--no-views", "--planet", "no", "--dry-run")
            self.assertEqual(code, 0)
            # k2_paroi was measured with the planet file and the run has none: skipped.
            self.assertIn("Terrain.Map.Go k2_paroi   (skipped: measured with the planet file, "
                          "the run has none)", out)
            self.assertIn("Terrain.Map.Go lisiere_tropicale_savane\n", out)
        with tempfile.TemporaryDirectory() as folder, self.assertRaises(SystemExit):
            run("run", "--machine", str(machine_file(folder)), "--before", "main", "--places",
                "fleuve_plaine", "--landings", "k2_paroi", "--dry-run")

    def test_every_module_of_the_driver_is_copied(self):
        """The driver runs in Unreal from the copy of DRIVER_FILES: a module left out only
        breaks there (an ImportError at the start of the session)."""
        here = Path(verify.__file__).resolve().parent
        for module in sorted(p.name for p in here.glob("ue_*.py")):
            self.assertIn(module, verify.DRIVER_FILES)
        for name in verify.DRIVER_FILES:
            self.assertTrue((here / name).is_file(), name)

    def test_no_machine_file(self):
        code, _, err = run("run", "--machine", "/no/machine.json", "--before", "main",
                           "--dry-run")
        self.assertEqual(code, 2)
        self.assertIn("machine.example.json", err)

    def test_sun(self):
        m = machine.parse_machine(json.loads(EXAMPLE.read_text(encoding="utf-8")) |
                                  {"workRoot": NOWHERE}, check_root=False)
        self.assertEqual(verify.parse_sun("falls", m), {"elevation": 50, "yaw": 116})
        self.assertEqual(verify.parse_sun("30,200", m), {"elevation": 30.0, "yaw": 200.0})
        self.assertIsNone(verify.parse_sun("none", m))
        with self.assertRaises(SystemExit):
            verify.parse_sun("dawn", m)


if __name__ == "__main__":
    unittest.main()
