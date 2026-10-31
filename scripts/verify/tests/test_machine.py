"""The machine file (machine.py): the example of the repository, the paths of Unreal, and the
work folder kept out of the temporary folder."""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import machine  # noqa: E402

EXAMPLE = Path(__file__).resolve().parents[1] / "machine.example.json"
WORK = "C:/TerrainVerify" if os.name == "nt" else "/srv/terrain-verify"


def example(**changes):
    data = json.loads(EXAMPLE.read_text(encoding="utf-8"))
    data["workRoot"] = WORK
    data.update(changes)
    return data


class Example(unittest.TestCase):
    def test_the_machine_of_the_user(self):
        m = machine.parse_machine(example())
        self.assertEqual(m.project_name, "TerrainSandbox")
        self.assertEqual(m.editor_target, "TerrainSandboxEditor")
        self.assertEqual(m.map, "/Game/TerrainTest/TerrainGasp")
        self.assertEqual(m.map_folder(), "TerrainTest")
        self.assertEqual([p.name for p in m.planets], ["planet.bin", "planet_20261019.bin"])
        self.assertEqual(m.sun["falls"], {"elevation": 50, "yaw": 116})
        self.assertEqual(m.seed, 20261019)
        self.assertIn("-unattended", m.editor_args)
        self.assertEqual(m.editor_exe("Windows").as_posix(),
                         "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/"
                         "UnrealEditor.exe")
        self.assertEqual(m.build_script("Windows").as_posix(),
                         "C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat")
        self.assertTrue(str(m.build_script("Linux")).endswith("Linux/Build.sh"))

    def test_the_example_says_planet_file_is_always_set(self):
        text = EXAMPLE.read_text(encoding="utf-8")
        self.assertNotIn("vide sur les anciennes", text)
        self.assertIn("always sets PlanetFile", text)


class Refused(unittest.TestCase):
    def test_bad_files(self):
        temp = Path(tempfile.gettempdir()) / "verify"
        bad = [
            example(workRoot=str(temp)),
            example(workRoot="relative/work"),
            example(workRoot=WORK + " two"),
            example(map="/Game/TerrainGasp"),
            example(map="/Plugin/X/Y"),
            example(project="C:/Projets/TerrainSandbox/TerrainSandbox.txt"),
        ]
        for data in bad:
            with self.subTest(data=data), self.assertRaises(machine.MachineError):
                machine.parse_machine(data)
        data = example()
        del data["engine"]
        with self.assertRaises(machine.MachineError):
            machine.parse_machine(data)

    def test_missing_file(self):
        with self.assertRaises(machine.MachineError) as caught:
            machine.load_machine(Path(tempfile.gettempdir()) / "no_machine.json")
        self.assertIn("machine.example.json", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
