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


FOAM = (235, 240, 245)


def version(folder, label, check_color=GROUND, seen=-30.0, killed=False, shift=0,
            up_color=SKY):
    folder = Path(folder) / label
    (folder / "views").mkdir(parents=True)
    views = []
    for place in ("a", "b"):
        first, repeat = folder / "views" / f"{place}.png", folder / "views" / f"{place}__r.png"
        pngio.write_png(first, textured(GROUND, shift))
        pngio.write_png(repeat, textured(GROUND, shift))
        views.append({"place": place, "file": str(first), "repeat": str(repeat)})
    check = folder / "views" / "_pitch_down_a.png"
    pngio.write_png(check, textured(check_color))
    up = folder / "views" / "_pitch_up_a.png"
    pngio.write_png(up, textured(up_color))
    walk = folder / "walk_a.csv"
    walks.write_walk(walk, [walks.Sample(0.1 * i, i, 0.0, 10.0, "walking", 9.95, float(i))
                            for i in range(10)])
    result = {"views": views, "materials": [{"script": "create_water_materials.py", "ok": True}],
              "pitchCheck": {"place": "a", "requested": -30.0, "seen": seen, "file": str(check),
                             "upRequested": 30.0, "upSeen": -seen, "upFile": str(up)},
              "walks": [{"place": "a", "heading": 90.0, "distance": 9.0, "file": str(walk),
                         "reason": "distance"}], "errors": [], "missingApi": []}
    outcome = {"exit_code": 0, "killed": killed, "reason": "no heartbeat" if killed else "exited",
               "seconds": 300.0, "last_beat": {"phase": "done", "n": 40, "done": True}}
    result["done"] = True
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


def run_of(folder, before, after, safety_diff=None):
    return {"title": "main vs x", "date": "20261031_2000", "folder": str(folder),
            "versions": [before, after], "places": ["a", "b"], "walks": ["a:90:9"],
            "walkPlaces": ["a"], "safety": safety_diff or {
                "files": 1234, "changed": [], "added": [], "removed": [],
                "planets": {"C:/TerrainEngine/planet.bin": True}}}


class Summary(unittest.TestCase):
    def test_a_clean_comparison(self):
        with tempfile.TemporaryDirectory() as folder:
            text = report.write_report(run_of(folder, version(folder, "avant"),
                                              version(folder, "apres", shift=40)))
            self.assertEqual(text.splitlines()[1], "Aucune alerte.")
            self.assertIn("Projet de l'utilisateur : intact (1234 fichiers", text.splitlines()[2])
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

    def test_an_inverted_pitch_is_a_notice(self):
        import verify
        for down, up, seen in ((SKY, GROUND, -30.0), (SKY, SKY, -30.0), (GROUND, SKY, 30.0)):
            with self.subTest(down=down, up=up, seen=seen), \
                    tempfile.TemporaryDirectory() as folder:
                run = run_of(folder, version(folder, "avant"),
                             version(folder, "apres", down, seen, up_color=up))
                text = report.write_report(run)
                self.assertIn("AVERTISSEMENT (apres) : inclinaison inversée ?", text.splitlines()[1])
                self.assertEqual(verify.exit_code(run), 0)  # a notice, no failure

    def test_foam_looked_down_at_is_no_inverted_pitch(self):
        """b7a3f0e: the check view of the basin of the fall looked down at its foam and mist (53 %
        of "sky"): with the view pitched up beside it, no notice."""
        with tempfile.TemporaryDirectory() as folder:
            text = report.write_report(run_of(folder, version(folder, "avant", FOAM),
                                              version(folder, "apres", FOAM)))
            self.assertEqual(text.splitlines()[1], "Aucune alerte.")
            self.assertIn("contrôle de l'inclinaison à a : la caméra dit -30.0° pour -30° et "
                          "30.0° pour +30° ; ciel 0 % vers le bas, 100 % vers le haut", text)

    def test_a_camera_that_keeps_its_own_pitch(self):
        with tempfile.TemporaryDirectory() as folder:
            text = report.write_report(run_of(folder, version(folder, "avant"),
                                              version(folder, "apres", GROUND, -5.0)))
            self.assertIn("n'a pas l'inclinaison demandée", text.splitlines()[1])

    def test_the_walk_against_the_core(self):
        with tempfile.TemporaryDirectory() as folder:
            after = version(folder, "apres")
            after["walkCores"] = {"a": [10.0] * 9 + [10.6]}
            text = report.write_report(run_of(folder, version(folder, "avant"), after))
            self.assertIn("pieds au départ 10.00 m (core 10.00 m), à la fin 10.00 m (core "
                          "10.60 m)", text)
            self.assertIn("sous le sol du core : 1 échantillon(s)", text)
            self.assertIn("cap suivi 90°", text)
            walk = Path(after["sessions"][0]["result"]["walks"][0]["file"])
            walks.write_walk(walk, [walks.Sample(0.1 * i, 0.0, -i, 10.0, "walking", 9.9, i)
                                    for i in range(10)])  # north instead of east
            text = report.write_report(run_of(folder, version(folder, "avant2"), after))
            self.assertIn("le pion n'a pas suivi le cap : 0° au lieu de 90° (écart 90°)", text)

    def test_the_commits_in_short_on_the_sheet(self):
        self.assertEqual(report._short({"sha": "b7a3f0e4f3d0bc6e220db6ce5db313980eeceffd",
                                        "ref": "claude/peaceful-bell-zb968r"}), "b7a3f0e4f3")
        self.assertEqual(report._short({"sha": None, "ref": "main"}), "main")

    def test_a_killed_session_and_a_missing_view(self):
        with tempfile.TemporaryDirectory() as folder:
            after = version(folder, "apres", killed=True)
            Path(after["sessions"][0]["result"]["views"][1]["file"]).unlink()
            text = report.write_report(run_of(folder, version(folder, "avant"), after))
            self.assertIn("ATTENTION (apres) : session 1 tuée : no heartbeat", text)
            self.assertIn("ATTENTION (apres) : vue absente : b", text)
            self.assertIn("b : vue absente", text)

    def test_the_editor_that_closed_at_once(self):
        """facfd09: the editor closed 3 frames after the map, the heartbeat at start, no
        result: alerts first, and a non-zero exit code."""
        import verify
        with tempfile.TemporaryDirectory() as folder:
            after = version(folder, "apres")
            session = after["sessions"][0]
            session["outcome"]["last_beat"] = {"phase": "start", "place": None, "n": 1,
                                               "done": False}
            session["result"] = None
            run = run_of(folder, version(folder, "avant"), after)
            text = report.write_report(run)
            head = "\n".join(text.splitlines()[1:4])
            self.assertIn("l'éditeur s'est fermé avant la fin du pilote (sortie 0, dernier "
                          "battement : phase start, n 1)", head)
            self.assertIn("session 1 sans résultat (result_1.json absent)", head)
            self.assertNotIn("Aucune alerte", text)
            self.assertEqual(verify.exit_code(run), 1)
            clean = run_of(folder, version(folder, "avant2"), version(folder, "apres2"))
            report.write_report(clean)
            self.assertEqual(verify.exit_code(clean), 0)

    def test_an_error_of_the_driver(self):
        with tempfile.TemporaryDirectory() as folder:
            after = version(folder, "apres")
            after["sessions"][0]["result"]["errors"] = [
                "Traceback (most recent call last):\n  File \"ue_driver.py\", line 74, in tick\n"
                "ValueError: generator already executing\n"]
            text = report.write_report(run_of(folder, version(folder, "avant"), after))
            self.assertIn("ATTENTION (apres) : session 1 : erreur du pilote : ValueError: "
                          "generator already executing", text)
            self.assertIn("erreurs Python : 1 (journal 0, pilote 1, scripts 0)", text)

    def test_every_missing_function_in_one_alert(self):
        with tempfile.TemporaryDirectory() as folder:
            after = version(folder, "apres")
            after["sessions"][0]["result"]["missingApi"] = [
                "GameplayStatics.get_all_actors_with_tag", "Pawn.add_movement_input"]
            text = report.write_report(run_of(folder, version(folder, "avant"), after))
            self.assertIn("ATTENTION (apres) : session 1 : API d'Unreal manquante : "
                          "GameplayStatics.get_all_actors_with_tag, Pawn.add_movement_input",
                          text)

    def test_a_view_and_a_walk_missing(self):
        with tempfile.TemporaryDirectory() as folder:
            after = version(folder, "apres")
            result = after["sessions"][0]["result"]
            result["views"][1]["file"] = None
            result["walks"] = []
            text = report.write_report(run_of(folder, version(folder, "avant"), after))
            self.assertIn("ATTENTION (apres) : vue absente : b", text)
            self.assertIn("ATTENTION (apres) : marche absente : a", text)

    def test_a_walk_refused_by_terrain_pawn(self):
        with tempfile.TemporaryDirectory() as folder:
            after = version(folder, "apres")
            message = ("Terrain.Pawn: over water (sea, lake or river): the pawn of the game is "
                       "not put down here, the view of the tools stays")
            after["sessions"][0]["result"]["walks"] = [
                {"place": "a", "heading": 90.0, "distance": 9.0, "file": None,
                 "reason": "pawn refused", "landing": {"state": "refused", "message": message}}]
            text = report.write_report(run_of(folder, version(folder, "avant"), after))
            self.assertIn(f"ATTENTION (apres) : marche refusée par Terrain.Pawn à a : {message} ; "
                          "pour marcher depuis ce lieu : --no-dev-commands", text)
            self.assertNotIn("marche absente : a", text)

    def test_the_project_of_the_user_changed(self):
        with tempfile.TemporaryDirectory() as folder:
            diff = {"files": 10, "changed": ["Content/TerrainEngine/M_TerrainWater.uasset"],
                    "added": [], "removed": [], "planets": {"C:/TerrainEngine/planet.bin": False}}
            text = report.write_report(run_of(folder, version(folder, "avant"),
                                              version(folder, "apres"), diff))
            self.assertIn("ATTENTION : le projet de l'utilisateur a changé pendant le run : "
                          "Content/TerrainEngine/M_TerrainWater.uasset, "
                          "C:/TerrainEngine/planet.bin", text)

    def test_a_version_that_did_not_build(self):
        with tempfile.TemporaryDirectory() as folder:
            broken = {"label": "apres", "ref": "x", "sessions": [],
                      "error": "the plugin of apres did not build: ubt.log\nerror C2065"}
            text = report.write_report(run_of(folder, version(folder, "avant"), broken))
            self.assertIn("ATTENTION (apres) : the plugin of apres did not build", text)


if __name__ == "__main__":
    unittest.main()
