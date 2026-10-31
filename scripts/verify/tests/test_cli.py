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
    path = Path(folder) / "machine.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = verify.main(list(argv))
    return code, out.getvalue(), err.getvalue()


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
                             "git clone --no-hardlinks", "TerrainSandboxEditor Win64 Development",
                             "-DTERRAIN_REPO=", "--planet-check C:/TerrainEngine/planet.bin",
                             "--sections relief,water -6185104.0 155924.0",
                             "--sections relief,water -26927500.0 -270336.0",
                             "--line -6185104.0 155924.0", "-ExecutePythonScript=",
                             "/Game/TerrainTest/TerrainGasp", "TERRAIN_VERIFY_JOB="):
                self.assertIn(expected, out)
            self.assertFalse(Path(NOWHERE).exists())

    def test_planet_no_probes_without_it(self):
        with tempfile.TemporaryDirectory() as folder:
            _, out, _ = run("run", "--machine", str(machine_file(folder)), "--before", "main",
                            "--places", "chute388_bassin", "--planet", "no", "--dry-run")
            self.assertIn("version : main", out)
            self.assertNotIn("--planet-check", out)
            self.assertNotIn("--planet ", out)


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
