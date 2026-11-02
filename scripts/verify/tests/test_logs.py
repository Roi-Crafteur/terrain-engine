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


TELEPORT = """LogTerrainEngine: Terrain World: BP_Hero_C_0 teleported to (-6185411.0, 156218.0) m
LogTerrainEngine: Terrain World: load 3 report
  not reached when reported: ground, 64 m
LogTerrainEngine: Terrain World: load 4 (teleport) started at (-6185411.0, 156218.0) m, 12 tiles shown, 80 pending
LogTerrainEngine: Terrain World: load 4 (teleport): ground at 0.41 s
"""


class Loads(unittest.TestCase):
    def test_the_load_started_and_its_report(self):
        self.assertIsNone(ue_log.load_ended(EDITOR.split("[200]")[0]))  # nothing started
        self.assertIsNone(ue_log.load_ended(EDITOR.split("[300]")[0]))  # started, not reported
        self.assertEqual(ue_log.load_ended(EDITOR),
                         {"n": 1, "reason": "spawn", "x": 0.0, "z": 0.0})

    def test_a_teleport_reports_the_previous_load_first(self):
        """#58: a teleport first reports the previous load as it stands (if it was not), then
        starts its own: that report is not the end of the new load."""
        self.assertIsNone(ue_log.load_ended(TELEPORT))
        done = TELEPORT + "LogTerrainEngine: Terrain World: load 4 report\n"
        self.assertEqual(ue_log.load_ended(done),
                         {"n": 4, "reason": "teleport", "x": -6185411.0, "z": 156218.0})

    def test_a_plugin_that_logs_no_start(self):
        reports = "LogTerrainEngine: Terrain World: load 2 report\n"
        self.assertIsNone(ue_log.load_ended(reports))
        self.assertEqual(ue_log.load_ended(reports, starts_logged=False)["n"], 2)
        self.assertIsNone(ue_log.load_ended("", starts_logged=False))


class PawnLanding(unittest.TestCase):
    """What Terrain.Pawn of agent 36 (TerrainDevPawn.cpp) did, from its lines in the log."""
    W = "[2026.11.01-20.00.00:000][  9]LogTerrainEngine: Warning: Terrain dev: "
    L = "[2026.11.01-20.00.01:000][ 70]LogTerrainEngine: Terrain dev: "

    def test_put_down_and_standing(self):
        self.assertIsNone(ue_log.pawn_landing(""))  # still landing
        slid_once = self.W + "Terrain.Pawn: the pawn of the game slid (2.1 m): spot 2 of 3\n"
        self.assertIsNone(ue_log.pawn_landing(slid_once))  # it tries the next spot
        done = ue_log.pawn_landing(slid_once + self.L + "the pawn of the game stands, spot 2 of 3")
        self.assertEqual(done["state"], "stands")

    def test_slid_everywhere(self):
        text = (self.W + "Terrain.Pawn: the pawn of the game slid from the 3 spots tried near the "
                "view (the last: 0.8 m); it stays where it is\n")
        self.assertEqual(ue_log.pawn_landing(text)["state"], "slid")

    def test_refused(self):
        for reason in ("over water (sea, lake or river): the pawn of the game is not put down "
                       "here, the view of the tools stays; move over dry ground first",
                       "no walkable ground within 40 m: too steep (52 degrees under the view, "
                       "the pawn walks up to 40); the pawn of the game is not put down here",
                       "the view is still loading: the pawn of the game is given back once it "
                       "is shown"):
            with self.subTest(reason=reason[:20]):
                got = ue_log.pawn_landing(self.W + "Terrain.Pawn: " + reason + "\n")
                self.assertEqual(got, {"state": "refused", "message": "Terrain.Pawn: " + reason})
        already = self.W + "the player already has the pawn of the game\n"
        self.assertEqual(ue_log.pawn_landing(already)["state"], "already")


class Features(unittest.TestCase):
    def test_commands_of_the_plugin(self):
        main = 'FAutoConsoleCommandWithWorldAndArgs Water(TEXT("Terrain.Water"), ...)'
        dev = ('{TEXT("Terrain.Goto"), TEXT("Terrain.Goto <x> <z>")}, {TEXT("Terrain.Look")}, '
               '{TEXT("Terrain.Views"), ...}, PawnCommand(TEXT("Terrain.Pawn"), ...)')
        loading = 'UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: load %d report"), n);'
        start = ('TEXT("Terrain World: load %d (%s) started at (%.1f, %.1f) m, %d tiles shown, '
                 '%d pending")')  # TerrainWorldLoading.cpp of main (#58)
        self.assertEqual(ue_log.plugin_features([main]),
                         {"loadReport": False, "loadStart": False, "goto": False, "look": False,
                          "viewMode": False, "pawn": False, "pawnLanding": False,
                          "loadTest": False})
        stands = ('TerrainDev::Log(FString::Printf(TEXT("the pawn of the game stands, spot %d of '
                  '%d"), ...')  # TerrainDevPawn.cpp of main (#57)
        self.assertTrue(ue_log.plugin_features([stands])["pawnLanding"])
        f = ue_log.plugin_features([main, dev, loading])
        self.assertTrue(f["goto"] and f["look"] and f["pawn"] and f["loadReport"])
        self.assertFalse(f["loadStart"])
        self.assertTrue(ue_log.plugin_features([loading, start])["loadStart"])
        self.assertFalse(f["viewMode"])  # Terrain.Views alone is not Terrain.View
        self.assertTrue(ue_log.plugin_features(['TEXT("Terrain.View")'])["viewMode"])


if __name__ == "__main__":
    unittest.main()
