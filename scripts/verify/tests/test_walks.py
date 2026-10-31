"""The test walks (walks.py): when a walk stops, its log, its summary against the ground of the
core."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import walks  # noqa: E402
from walks import Sample  # noqa: E402


def walk(steps, mode_at=None, feet_at=None, speed=5.0, dt=0.1):
    """Samples every dt seconds along x at speed m/s, the mode and the feet chosen by sample."""
    out = []
    for i in range(steps):
        t = i * dt
        mode = mode_at(i) if mode_at else "walking"
        feet = feet_at(i) if feet_at else 100.0
        out.append(Sample(t, speed * t, 0.0, feet, mode, feet - 0.1, speed * t))
    return out


def stop(samples, watch):
    for s in samples:
        reason = watch.feed(s)
        if reason:
            return reason, s
    return None, samples[-1]


class Watch(unittest.TestCase):
    def test_distance(self):
        reason, at = stop(walk(300), walks.WalkWatch(goal=50.0))
        self.assertEqual(reason, "distance")
        self.assertGreaterEqual(at.distance, 50.0)

    def test_first_fall(self):
        # Falling from sample 40: stopped 0.3 s later, at the first fall.
        samples = walk(300, mode_at=lambda i: "falling" if 40 <= i < 60 else "walking")
        reason, at = stop(samples, walks.WalkWatch(goal=1000.0))
        self.assertEqual(reason, "fall")
        self.assertAlmostEqual(at.t, 4.3, places=6)

    def test_a_short_hop_is_no_fall(self):
        samples = walk(300, mode_at=lambda i: "falling" if 40 <= i < 42 else "walking")
        reason, _ = stop(samples, walks.WalkWatch(goal=100.0))
        self.assertEqual(reason, "distance")

    def test_a_drop_stops_at_once(self):
        samples = walk(300, mode_at=lambda i: "falling" if i >= 40 else "walking",
                       feet_at=lambda i: 100.0 if i < 41 else 97.0)
        reason, at = stop(samples, walks.WalkWatch(goal=1000.0))
        self.assertEqual((reason, round(at.t, 6)), ("fall", 4.1))

    def test_stuck_and_timeout(self):
        reason, _ = stop(walk(300, speed=0.05), walks.WalkWatch(goal=100.0))
        self.assertEqual(reason, "stuck")
        reason, _ = stop(walk(300, speed=1.0), walks.WalkWatch(goal=1000.0, timeout=10.0))
        self.assertEqual(reason, "timeout")


class Log(unittest.TestCase):
    def test_write_read(self):
        samples = walk(5)
        samples[2] = Sample(0.2, 1.0, 0.0, 100.0, "walking", None, 1.0)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "walk.csv"
            walks.write_walk(path, samples)
            back = walks.read_walk(path)
        self.assertEqual(len(back), 5)
        self.assertIsNone(back[2].ground)
        self.assertAlmostEqual(back[4].distance, samples[4].distance, places=3)

    def test_summary(self):
        feet = [100.0, 100.2, 100.4, 101.4, 101.5, 99.0, 98.0, 98.1]
        modes = ["walking"] * 5 + ["falling", "falling", "walking"]
        samples = [Sample(0.1 * i, i, 0.0, feet[i], modes[i], feet[i] - 0.05, float(i))
                   for i in range(8)]
        profile = [(0.0, 100.0), (7.0, 99.0)]
        s = walks.summarize(samples, "fall", profile, tolerance=0.5)
        self.assertEqual((s["falls"], s["firstFallAt"], s["reason"]), (1, 5.0, "fall"))
        self.assertAlmostEqual(s["largestStep"], 1.0)
        self.assertAlmostEqual(s["leastClearance"], 0.05)
        # Below the core ground (100 - d/7) by more than 0.5 m: samples 6 and 7.
        self.assertEqual(s["belowCoreGround"], 2)
        self.assertEqual(walks.interpolated(profile, 3.5), 99.5)
        self.assertEqual(walks.interpolated(profile, 20.0), 99.0)

    def test_parse(self):
        self.assertEqual(walks.parse_walk("a"), {"place": "a", "heading": None, "distance": 100.0})
        self.assertEqual(walks.parse_walk("a:227:60"),
                         {"place": "a", "heading": 227.0, "distance": 60.0})
        self.assertEqual(walks.parse_walk("a::60")["heading"], None)
        for bad in ("", ":1", "a:1:2:3", "a:x", "a:1:-5"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                walks.parse_walk(bad)


if __name__ == "__main__":
    unittest.main()
