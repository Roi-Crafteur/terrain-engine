"""The check that the project of the user was left as it was (safety.py), without git: a file
changed, added or removed is found, a planet changed too; Saved and our plugin are left out."""

import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fakes  # noqa: E402
import safety  # noqa: E402


class Snapshot(unittest.TestCase):
    def test_untouched(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            before = safety.snapshot(m)
            self.assertIn("Content/TerrainTest/TerrainGasp.umap", before["files"])
            self.assertFalse(any(k.startswith(("Saved/", "Binaries/", "Plugins/terrain-engine/"))
                                 for k in before["files"]))
            # What the editor of the user may write is not counted.
            (m.project.parent / "Saved" / "Logs").mkdir(parents=True)
            (m.project.parent / "Saved" / "Logs" / "TerrainSandbox.log").write_text("x")
            diff = safety.compare(before, safety.snapshot(m))
            self.assertTrue(safety.untouched(diff))
            self.assertEqual(diff["files"], len(before["files"]))

    def test_changed_added_removed(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            before = safety.snapshot(m)
            project = m.project.parent
            asset = project / "Content" / "TerrainEngine" / "M_TerrainWater.uasset"
            asset.write_bytes(b"material rebuilt")  # another size
            stamp = time.time() + 5.0
            ini = project / "Config" / "DefaultEngine.ini"
            os.utime(ini, (stamp, stamp))  # same size, another date
            (project / "Content" / "BigPack" / "T_New.uasset").write_bytes(b"new")
            (project / "Content" / "TerrainTest" / "TerrainGasp.umap").unlink()
            diff = safety.compare(before, safety.snapshot(m))
            self.assertEqual(diff["changed"], ["Config/DefaultEngine.ini",
                                               "Content/TerrainEngine/M_TerrainWater.uasset"])
            self.assertEqual(diff["added"], ["Content/BigPack/T_New.uasset"])
            self.assertEqual(diff["removed"], ["Content/TerrainTest/TerrainGasp.umap"])
            self.assertFalse(safety.untouched(diff))

    def test_a_planet_written(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            before = safety.snapshot(m)
            planet = m.planets[0]
            stat = planet.stat()
            planet.write_bytes(b"user planEt")  # same size
            os.utime(planet, ns=(stat.st_atime_ns, stat.st_mtime_ns))  # same date
            diff = safety.compare(before, safety.snapshot(m))
            self.assertEqual(diff["planets"], {str(planet): False})
            self.assertFalse(safety.untouched(diff))


if __name__ == "__main__":
    unittest.main()
