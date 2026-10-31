"""What the logs of Unreal say (ue_log.py), on excerpts written as UBT and the editor write
them."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import ue_log  # noqa: E402

UBT_OK = """Using bundled DotNet SDK version: 8.0.300
Building TerrainSandboxEditor and ShaderCompileWorker - Win64 - Development
[1/12] Compile [x64] core_generation_noise.cpp
C:\\x\\TerrainWorld.cpp(42): warning C4996: 'Foo': deprecated
[12/12] Link [x64] UnrealEditor-TerrainEngine.dll
Total execution time: 182.40 seconds
Result: Succeeded
"""

UBT_FAILED = """[3/12] Compile [x64] TerrainWorld.cpp
C:\\x\\TerrainWorld.cpp(120): error C2065: 'Pawnn': undeclared identifier
LINK : fatal error LNK1181: cannot open input file 'x.lib'
Result: Failed (OtherCompilationError)
"""

EDITOR = """[2026.10.31-12.00.01:000][  0]LogInit: Display: Running engine for game: TerrainSandbox
[2026.10.31-12.00.20:000][ 10]LogPython: Error: Traceback (most recent call last):
[2026.10.31-12.00.20:000][ 10]LogPython: Error:   File "create_water_materials.py", line 3
[2026.10.31-12.00.40:000][ 50]LogShaderCompilers: Error: Failed to compile shader for M_TerrainWater
[2026.10.31-12.01.00:000][ 90]LogTerrainEngine: the heights and tiles of the default world match terrain_core bit for bit
[2026.10.31-12.01.05:000][ 95]LogTerrainEngine: planet level read from C:/v/Saved/TerrainEngine/planet_20261019.bin in 3.1 s
[2026.10.31-12.01.30:000][200]LogTerrainEngine: Terrain World: load 1 (spawn) started at (0.0, 0.0) m
[2026.10.31-12.01.40:000][300]LogTerrainEngine: Terrain World: load 1 report
[2026.10.31-12.02.10:000][400]LogTerrainEngine: Warning: no M_TerrainWater material set
[2026.10.31-12.02.30:000][500]LogTerrainEngine: Terrain World: load 2 report
"""


class Build(unittest.TestCase):
    def test_succeeded(self):
        r = ue_log.scan_build(UBT_OK)
        self.assertEqual((r["result"], r["errors"], r["warnings"]), ("Succeeded", [], 1))

    def test_failed(self):
        r = ue_log.scan_build(UBT_FAILED)
        self.assertEqual(r["result"], "Failed")
        self.assertEqual(len(r["errors"]), 2)
        self.assertIn("C2065", r["errors"][0])

    def test_errors_without_result(self):
        self.assertEqual(ue_log.scan_build("a.cpp(1): error C1083: no file\n")["result"],
                         "Failed")
        self.assertIsNone(ue_log.scan_build("nothing\n")["result"])


class Editor(unittest.TestCase):
    def test_session(self):
        r = ue_log.scan_editor(EDITOR)
        self.assertEqual(len(r["pythonErrors"]), 2)
        self.assertEqual(len(r["shaderErrors"]), 1)
        self.assertIs(r["determinism"], True)
        self.assertFalse(r["planetBuilt"])
        self.assertEqual(r["loadReports"], [1, 2])
        self.assertEqual(r["terrainProblems"], ["Warning: no M_TerrainWater material set"])
        self.assertEqual(r["crash"], [])

    def test_bad_world_and_built_planet(self):
        text = ("LogTerrainEngine: Error: this build does not generate the same world\n"
                "LogTerrainEngine: Warning: planet level BUILT (missing) in 114 s, written to x\n"
                "LogWindows: Error: === Critical error: ===\n")
        r = ue_log.scan_editor(text)
        self.assertIs(r["determinism"], False)
        self.assertTrue(r["planetBuilt"])
        self.assertEqual(len(r["crash"]), 1)

    def test_read_text(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "log.txt"
            path.write_bytes("Résultat\n".encode("latin-1"))
            self.assertEqual(ue_log.read_text(path), "Résultat\n")
            path.write_bytes("é\n".encode("utf-8"))
            self.assertEqual(ue_log.read_text(path), "é\n")


class Features(unittest.TestCase):
    def test_commands_of_the_plugin(self):
        main = 'FAutoConsoleCommandWithWorldAndArgs Water(TEXT("Terrain.Water"), ...)'
        dev = ('{TEXT("Terrain.Goto"), TEXT("Terrain.Goto <x> <z>")}, {TEXT("Terrain.Look")}, '
               '{TEXT("Terrain.Views"), ...}, PawnCommand(TEXT("Terrain.Pawn"), ...)')
        loading = 'UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: load %d report"), n);'
        self.assertEqual(ue_log.plugin_features([main]),
                         {"loadReport": False, "goto": False, "look": False, "viewMode": False,
                          "pawn": False, "loadTest": False})
        f = ue_log.plugin_features([main, dev, loading])
        self.assertTrue(f["goto"] and f["look"] and f["pawn"] and f["loadReport"])
        self.assertFalse(f["viewMode"])  # Terrain.Views alone is not Terrain.View
        self.assertTrue(ue_log.plugin_features(['TEXT("Terrain.View")'])["viewMode"])


if __name__ == "__main__":
    unittest.main()
