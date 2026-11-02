"""What the core of a version says of the places (probe.py): the JSON of terrain_probe, the text
of terrain_probe_water before it, the frame and the eye, the ground along a walk."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fakes  # noqa: E402
import places  # noqa: E402
import probe  # noqa: E402
import planet  # noqa: E402
import workspace  # noqa: E402

PROBE_WATER = """planet level read from C:/v/Saved/TerrainEngine/planet_20261019.bin
point (-6185104.0, 155924.0):
  relief 216.30 m, ground 178.82 m; water: river at 180.03 m, 1.21 m deep
  rivers: carved yes, valley 178.82 m, wet yes, level 180.03 m, floor 216.45 m
  lake: none
point (416417.0, -11415206.0):
  relief 6687.01 m, ground 6687.01 m; water: dry
point (429908.0, -855305.0):
  relief -1.47 m, ground -1.47 m; water: sea at 0.00 m, 1.47 m deep
"""


def probe_json(points):
    return json.dumps({"tool": "terrain_probe", "format": 1, "points": [
        {"x": x, "z": z, "relief": {"relief": g + 1.0, "ground": g},
         "water": {"kind": k, "level": l}} for x, z, g, k, l in points]})


class Runner(fakes.FakeRunner):
    def __init__(self, answer):
        super().__init__()
        self.answer = answer

    def run(self, command, log=None, cwd=None, check=True):
        command = [str(c) for c in command]
        if "terrain_probe" in command[0]:
            self.commands.append(command)
            return 0, self.answer
        return super().run(command, log, cwd, check)


class Parse(unittest.TestCase):
    def test_probe_water(self):
        cols = probe.parse_probe_water(PROBE_WATER)
        self.assertEqual(cols[0], probe.Column(178.82, 216.30, "river", 180.03))
        self.assertEqual(cols[1], probe.Column(6687.01, 6687.01, "dry", None))
        self.assertEqual(cols[2].water, "sea")
        self.assertEqual(cols[2].level, 0.0)

    def test_probe_json(self):
        cols = probe.parse_probe_json(probe_json([(1, 2, 178.818, "river", 180.028),
                                                   (3, 4, 9.6, "dry", None)]))
        self.assertEqual(cols[0], probe.Column(178.818, 179.818, "river", 180.028))
        self.assertIsNone(cols[1].level)


class Columns(unittest.TestCase):
    def test_both_probes(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            for kind, answer in (("probe", probe_json([(0, 0, 1.0, "dry", None)])),
                                 ("probe_water", PROBE_WATER.split("point (416417")[0])):
                v = workspace.prepare(m, "main", kind, fakes.FakeRunner(probe_kind=kind),
                                      fresh=True)
                runner = Runner(answer)
                cols = probe.columns(runner, v, m, [(0.0, 0.0)], v.project_dir / "p.bin")
                self.assertEqual(len(cols), 1)
                command = runner.commands[-1]
                self.assertIn("--planet", command)
                self.assertEqual("--json" in command, kind == "probe")

    def test_never_the_planet_of_the_user(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            v = workspace.prepare(m, "main", "a", fakes.FakeRunner())
            with self.assertRaises(planet.PlanetError):
                probe.columns(Runner(""), v, m, [(0.0, 0.0)], m.planets[0])

    def test_a_missing_point(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            v = workspace.prepare(m, "main", "a", fakes.FakeRunner())
            with self.assertRaises(probe.ProbeError):
                probe.columns(Runner(probe_json([])), v, m, [(0.0, 0.0)])


class Frame(unittest.TestCase):
    def test_eye(self):
        basin = places.Place("b", -6185104, 155924, 227, 30, 182.03, "altitude", True)
        lip = places.Place("l", -6185411, 156218, 46, -30, 2.0, "ground", True)
        f = probe.frame_of(probe.Column(178.818, 216.3, "river", 180.028), basin)
        self.assertEqual(f["eye"], 182.03)
        self.assertAlmostEqual(f["eyeZ"], 321.2)
        g = probe.frame_of(probe.Column(567.781, 611.7, "river", 568.991), lip)
        self.assertAlmostEqual(g["eye"], 570.991)
        self.assertAlmostEqual(g["eyeZ"], 321.0)

    def test_walk_profile(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            v = workspace.prepare(m, "main", "a", fakes.FakeRunner())
            line = {"line": {"points": [{"distance": 0.0, "relief": {"ground": 10.0}},
                                        {"distance": 2.0, "relief": {"ground": 10.5}}]}}
            runner = Runner(json.dumps(line))
            profile = probe.walk_profile(runner, v, m, (100.0, 200.0), 90.0, 2.0)
            self.assertEqual(profile, [(0.0, 10.0), (2.0, 10.5)])
            command = runner.commands[-1]
            at = command.index("--line")
            self.assertEqual([float(c) for c in command[at + 1:at + 6]],
                             [100.0, 200.0, 102.0, 200.0, 2.0])


if __name__ == "__main__":
    unittest.main()
