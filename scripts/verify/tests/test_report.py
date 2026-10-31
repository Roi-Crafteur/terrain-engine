"""The summary of a verification (report.py): resume.txt with its alerts first (an inverted pitch,
a killed session), the noise of each view with itself, the walks, the differences before / after,
the contact sheet."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pngio  # noqa: E402
import report  # noqa: E402
import walks  # noqa: E402

GROUND = (90, 70, 50)
SKY = (120, 170, 230)


def textured(color, seed=0):
    image = pngio.blank(32, 18, color)
    if color == GROUND:
        for i in range(0, len(image.rgb), 3):
            image.rgb[i] = (image.rgb[i] + 7 * (i // 3) + seed) % 256
    return image


def version(folder, label, check_color=GROUND, seen=-30.0, killed=False, shift=0):
    folder = Path(folder) / label
    (folder / "views").mkdir(parents=True)
    views = []
    for place in ("a", "b"):
        first, repeat = folder / "views" / f"{place}.png", folder / "views" / f"{place}__r.png"
        pngio.write_png(first, textured(GROUND, shift))
        pngio.write_png(repeat, textured(GROUND, shift))
        views.append({"place": place, "file": str(first), "repeat": str(repeat)})
    check = folder / "views" / "_pitch_check_a.png"
    pngio.write_png(check, textured(check_color))
    walk = folder / "walk_a.csv"
    walks.write_walk(walk, [walks.Sample(0.1 * i, i, 0.0, 10.0, "walking", 9.95, float(i))
                            for i in range(10)])
    result = {"views": views, "materials": [{"script": "create_water_materials.py", "ok": True}],
              "pitchCheck": {"place": "a", "requested": -30.0, "seen": seen, "file": str(check)},
              "walks": [{"place": "a", "heading": 90.0, "distance": 9.0, "file": str(walk),
                         "reason": "distance"}], "errors": [], "missingApi": []}
    outcome = {"exit_code": 0, "killed": killed, "reason": "no heartbeat" if killed else "exited",
               "seconds": 300.0}
    scan = {"shaderErrors": [], "pythonErrors": [], "loadReports": [1, 2], "determinism": True,
            "planetBuilt": False, "terrainProblems": [], "crash": []}
    return {"label": label, "ref": label + "-ref", "sha": "0123456789abcdef", "build":
            {"result": "Succeeded", "warnings": 2}, "cached": True,
            "planet": {"source": "C:/TerrainEngine/planet.bin", "origin": "machine",
                       "checksum": "0x26626564570b0d9e", "installed": "P/planet.bin"},
            "sessions": [{"places": ["a", "b"], "outcome": outcome, "result": result,
                          "scan": scan}],
            "probes": {"a": {"ground": 10.0, "waterLevel": None, "eye": 11.8}},
            "profiles": {"a": [(0.0, 10.0), (9.0, 10.0)]}}


def run_of(folder, before, after):
    return {"title": "main vs x", "date": "20261031_2000", "folder": str(folder),
            "versions": [before, after], "places": ["a", "b"], "walks": ["a:90:9"]}


class Summary(unittest.TestCase):
    def test_a_clean_comparison(self):
        with tempfile.TemporaryDirectory() as folder:
            text = report.write_report(run_of(folder, version(folder, "avant"),
                                              version(folder, "apres", shift=40)))
            self.assertEqual(text.splitlines()[1], "Aucune alerte.")
            self.assertIn("== avant : avant-ref (0123456789)", text)
            self.assertIn("build : Succeeded (cache du commit), 2 avertissement(s)", text)
            self.assertIn("somme 0x26626564570b0d9e", text)
            self.assertIn("scripts de matériaux : 1/1 ok", text)
            self.assertIn("chargements rapportés : 2 ; déterminisme : ok", text)
            self.assertIn("a : moyenne 0.00/255, 0.00 % des pixels à plus de 24", text)  # noise
            self.assertIn("arrêt « distance » à 9.0 m", text)
            self.assertIn("sous le sol du core : 0 échantillon(s)", text)
            self.assertIn("== écarts de pixels avant / après", text)
            sheet = pngio.read_png(Path(folder) / "planche.png", use_pillow=False)
            self.assertGreater(sheet.width, 3 * 32)
            self.assertTrue((Path(folder) / "planche.html").is_file())
            self.assertTrue((Path(folder) / "run.json").is_file())

    def test_an_inverted_pitch_comes_first(self):
        for color, seen in ((SKY, -30.0), (GROUND, 30.0)):
            with self.subTest(color=color, seen=seen), tempfile.TemporaryDirectory() as folder:
                text = report.write_report(run_of(folder, version(folder, "avant"),
                                                  version(folder, "apres", color, seen)))
                self.assertIn("inclinaison inversée ?", text.splitlines()[1])
                self.assertIn("(apres)", text.splitlines()[1])

    def test_a_camera_that_keeps_its_own_pitch(self):
        with tempfile.TemporaryDirectory() as folder:
            text = report.write_report(run_of(folder, version(folder, "avant"),
                                              version(folder, "apres", GROUND, -5.0)))
            self.assertIn("n'a pas l'inclinaison demandée", text.splitlines()[1])

    def test_a_killed_session_and_a_missing_view(self):
        with tempfile.TemporaryDirectory() as folder:
            after = version(folder, "apres", killed=True)
            Path(after["sessions"][0]["result"]["views"][1]["file"]).unlink()
            text = report.write_report(run_of(folder, version(folder, "avant"), after))
            self.assertIn("session tuée : no heartbeat", text)
            self.assertIn("b : vue absente", text)

    def test_a_version_that_did_not_build(self):
        with tempfile.TemporaryDirectory() as folder:
            broken = {"label": "apres", "ref": "x", "sessions": [],
                      "error": "the plugin of apres did not build: ubt.log\nerror C2065"}
            text = report.write_report(run_of(folder, version(folder, "avant"), broken))
            self.assertIn("ATTENTION (apres) : the plugin of apres did not build", text)


if __name__ == "__main__":
    unittest.main()
