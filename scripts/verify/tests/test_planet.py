"""The planet file of a version (planet.py): a planet of the user reused when the version accepts
it, one built beside otherwise, the files of the user never written."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fakes  # noqa: E402
import planet  # noqa: E402
import workspace  # noqa: E402


def version_of(m, kind="probe"):
    return workspace.prepare(m, "main", "avant", fakes.FakeRunner(probe_kind=kind))


class Choose(unittest.TestCase):
    def test_the_planet_of_the_user_when_accepted(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            v = version_of(m)
            runner = fakes.FakeRunner(planet_status={"planet_20261019.bin": "ok"})
            choice = planet.choose(m, v, runner)
            self.assertEqual((choice.path, choice.origin), (m.planets[0], "machine"))
            self.assertEqual(choice.checksum, "0x26626564570b0d9e")
            self.assertFalse(any("terrain_make_planet" in " ".join(c) for c in runner.commands))

    def test_built_beside_when_none_is_accepted(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            v = version_of(m)
            runner = fakes.FakeRunner(planet_status={})  # every file stale
            choice = planet.choose(m, v, runner)
            self.assertEqual(choice.origin, "built now")
            self.assertEqual(choice.path, m.work_root / "planets" / "1111222233334444.bin")
            self.assertEqual(m.planets[0].read_bytes(), b"user planet")
            # The next version finds it there.
            runner = fakes.FakeRunner(planet_status={"1111222233334444.bin": "ok"})
            again = planet.choose(m, v, runner)
            self.assertEqual((again.path, again.origin), (choice.path, "built before"))

    def test_a_version_before_the_probe_builds_its_own(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            v = version_of(m, kind="probe_water")
            choice = planet.choose(m, v, fakes.FakeRunner(planet_status={}))
            self.assertEqual(choice.path.name, f"commit_{fakes.SHA}.bin")
            self.assertEqual(choice.checksum, "0x1111222233334444")


class Install(unittest.TestCase):
    def test_copied_into_the_project_of_the_version(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            v = version_of(m)
            choice = planet.PlanetChoice(m.planets[0], "0x1", "machine")
            path = planet.install(choice, v, m)
            self.assertEqual(path, (v.project_dir / "Saved" / "TerrainEngine" /
                                    "planet_20261019.bin").resolve())
            self.assertEqual(path.read_bytes(), b"user planet")
            self.assertEqual(m.planets[0].read_bytes(), b"user planet")

    def test_never_over_a_file_of_the_user(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            with self.assertRaises(planet.PlanetError):
                planet.guard(m.planets[0], m)
            v = version_of(m)
            v.project_dir = m.project.parent  # a project that would be the one of the user
            with self.assertRaises(planet.PlanetError):
                planet.install(planet.PlanetChoice(m.planets[0], "0x1", "machine"), v, m)
            self.assertEqual(m.planets[0].read_bytes(), b"user planet")


if __name__ == "__main__":
    unittest.main()
