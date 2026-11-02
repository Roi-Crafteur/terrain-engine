"""The landings of the driver (ue_landings.py) against the stand-in plugin of fake_map.py:
Terrain.Map.Go <place> (agent 35), one at a time, read from the lines written after its command
only; the pawn of the game on land, the view of the tools over water; the planet places skipped
without the planet file; the session stopped after landings without an end."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import fake_map  # noqa: E402
import fake_unreal  # noqa: E402
import landings as landings_module  # noqa: E402
import report  # noqa: E402
import verify  # noqa: E402
from test_driver import BASIN, EDGE, K2, calls, drive, job_in  # noqa: E402
from test_report import run_of, version  # noqa: E402

FEATURES = {"goto": True, "look": True, "viewMode": True, "pawn": True, "loadReport": True,
            "loadStart": True, "mapGo": True}
SEA = {"name": "falaise_cotiere", "x": 4944509.0, "z": -8706669.0, "heading": 225.0,
       "pitch": 21.4, "yaw": 135.0, "eye": 2.0, "eyeMode": "ground", "usePlanet": True,
       "ground": -8.51, "waterLevel": 0.0, "eyeAltitude": 2.0}
OLD_OK = ("LogTerrainEngine: Terrain Dev Map: 1 s after the click: the pawn of the game, Walking, "
          "0.00 m/s, feet at 1.94 m (no water within 3 m), 0.0 m from the click: OK\n")


def landings(fake, places, outcomes=None, features=FEATURES, ready_after=0.5, **changes):
    """The driver over places, a landing at each, without the views (--no-views)."""
    folder = fake.folder
    job = job_in(folder, fake, places, materials=False, views=False, pitchCheck=False,
                 landings=[{"place": p["name"], "skip": None} for p in places],
                 landingTimeout=8.0, **changes)
    job["features"] = dict(features)
    go = fake_map.MapGo(fake, places, outcomes, ready_after)
    return drive(fake, job).result, go


class Landing(unittest.TestCase):
    def test_on_land_the_line_of_the_check(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            r, _ = landings(fake, [EDGE])
            self.assertTrue(r["done"], r["errors"])
            (entry,) = r["landings"]
            self.assertEqual((entry["decision"], entry["end"]), ("land", "check"))
            self.assertTrue(entry["check"]["ok"])
            self.assertEqual(entry["check"]["who"], "the pawn of the game")
            # The feet of the pawn in the frame of the core, for the launcher to probe there.
            self.assertEqual([round(v, 3) for v in entry["feet"]],
                             [EDGE["x"], EDGE["z"], EDGE["ground"]])
            self.assertFalse(calls(fake, "begin_play")[1:])  # one play
            commands = [c[1] for c in calls(fake, "console")]
            self.assertEqual(commands, ["Terrain.Map.Go lisiere"])  # no view: no Goto, no shot

    def test_the_wall_of_k2_puts_the_pawn_38_m_away(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            r, _ = landings(fake, [K2], {"k2_paroi": {"away": 38.0, "feet": 6662.0}})
            (entry,) = r["landings"]
            self.assertEqual((entry["end"], entry["check"]["away"]), ("check", 38.0))
            self.assertAlmostEqual(entry["feet"][0], K2["x"] + 38.0, places=3)

    def test_over_water_the_view_of_the_tools(self):
        """No line of the check over water: the decision ends it; the eye read once the load of
        its teleport is reported."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            r, _ = landings(fake, [SEA], {"falaise_cotiere": {"kind": "water"}})
            (entry,) = r["landings"]
            self.assertEqual((entry["decision"], entry["end"], entry["check"]),
                             ("water", "water", None))
            self.assertEqual(entry["radius"], 50.0)
            self.assertAlmostEqual(entry["eye"][2], 1.8, places=6)

    def test_an_old_line_ok_does_not_count(self):
        """Run 5 (spawn): only the lines written after the command count."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            with open(fake.log_path, "a", encoding="utf-8") as log:
                log.write(OLD_OK)
            r, _ = landings(fake, [EDGE], {"lisiere": {"kind": "silent"}}, landingMisses=5)
            (entry,) = r["landings"]
            self.assertEqual(entry["end"], "none")  # the old OK is no end of this landing
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            with open(fake.log_path, "a", encoding="utf-8") as log:
                log.write(OLD_OK)
            r, _ = landings(fake, [EDGE], {"lisiere": {"feet": 263.9}})
            self.assertEqual(r["landings"][0]["check"]["feet"], 263.9)  # its own line, not 1.94

    def test_one_landing_at_a_time(self):
        """The next command waits for the end of the previous one (a new one would replace it)."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            r, go = landings(fake, [EDGE, K2], {"k2_paroi": {"away": 38.0}})
            self.assertEqual([n for n, _ in go.sent], ["lisiere", "k2_paroi"])
            log = fake.log_path.read_text(encoding="utf-8")
            first_end = log.index("1 s after the click")
            self.assertGreater(go.sent[1][1], len(log[:first_end].encode("utf-8")))
            self.assertEqual([e["check"]["away"] for e in r["landings"]], [0.0, 38.0])

    def test_landings_without_an_end_stop_the_session(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            places = [EDGE, K2, dict(EDGE, name="third", x=EDGE["x"] + 500.0)]
            silent = {"kind": "silent"}
            r, go = landings(fake, places, {"lisiere": silent, "k2_paroi": silent})
            self.assertEqual(r["stopped"], {"misses": 2, "after": "k2_paroi"})
            self.assertEqual([n for n, _ in go.sent], ["lisiere", "k2_paroi"])  # never third
            self.assertTrue(r["done"])  # the driver ends: main() quits the editor on the next tick
            self.assertEqual(len(calls(fake, "end_play")), 1)
            self.assertLess(fake.time, 30.0)  # 2 x 8 s, not every place

    def test_refused_and_fallback(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            r, _ = landings(fake, [BASIN, EDGE], {"chute388_bassin": {"kind": "fallback"},
                                                   "lisiere": {"kind": "refused"}})
            fallback, refused = r["landings"]
            self.assertEqual((fallback["decision"], fallback["end"]), ("shallow", "fallback"))
            self.assertAlmostEqual(fallback["eye"][2], BASIN["waterLevel"] + 1.8, places=6)
            self.assertEqual(refused["end"], "refused")
            self.assertIn("No landing for lisiere", refused["message"])

    def test_the_warning_of_the_planet_file_is_kept(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            warning = ("was measured without the planet file: this world has it, whose "
                       "hydrology changes the ground near its rivers")
            r, _ = landings(fake, [EDGE], {"lisiere": {"warning": warning}})
            (entry,) = r["landings"]
            self.assertEqual((entry["end"], entry["planetWarning"]), ("check", warning))

    def test_the_world_not_ready_yet(self):
        """Real run 6 (9d49fa0, --no-views): Terrain.Map.Go sent 23 ms after the start of the
        play, while the world still built: "no world playing" for all 11 places. Now the first
        landing waits for the first load and the dev map started, then lands."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            seen = []
            fake.console_hooks.append(lambda c: seen.append((c, fake.log_path.read_text())))
            r, go = landings(fake, [EDGE], features=dict(FEATURES, mapStarted=True),
                             ready_after=6.0)  # longer than the first load (3 s)
            self.assertTrue(r["done"], r["errors"])
            self.assertEqual(r["landings"][0]["end"], "check")
            log = fake.log_path.read_text(encoding="utf-8")
            self.assertNotIn("no world playing", log)
            sent = next(text for command, text in seen if command.startswith("Terrain.Map.Go"))
            self.assertIn("Terrain Dev Map: started (", sent)  # sent once the map was there
            self.assertIn("load 1 report", sent)
            self.assertGreaterEqual(r["ready"][0], 5.5)

    def test_the_views_wait_for_the_world_too(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            seen = []
            fake.console_hooks.append(lambda c: seen.append((c, fake.log_path.read_text())))
            job = job_in(folder, fake, [EDGE], materials=False, pitchCheck=False,
                         landings=[{"place": "lisiere", "skip": None}], landingTimeout=8.0)
            job["features"] = dict(FEATURES, mapStarted=True)
            fake_map.MapGo(fake, [EDGE], ready_after=6.0)
            r = drive(fake, job).result
            self.assertTrue(r["done"], r["errors"])
            goto = next(text for command, text in seen if command.startswith("Terrain.Goto"))
            self.assertIn("Terrain Dev Map: started (", goto)
            self.assertEqual([v["loaded"] for v in r["views"]], ["reported"])
            self.assertEqual(r["landings"][0]["end"], "check")

    def test_sent_too_early_counts_for_the_stop(self):
        """ "no world playing" is no outcome of a landing but a problem of the launcher: two in a
        row stop the session."""
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            early = {"kind": "notready"}
            r, go = landings(fake, [EDGE, K2, dict(EDGE, name="third", x=EDGE["x"] + 500.0)],
                             {"lisiere": early, "k2_paroi": early})
            self.assertEqual([e["end"] for e in r["landings"]], ["notReady", "notReady"])
            self.assertEqual(r["stopped"], {"misses": 2, "after": "k2_paroi"})
            self.assertEqual([n for n, _ in go.sent], ["lisiere", "k2_paroi"])

    def test_a_world_never_ready(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            r, go = landings(fake, [EDGE], features=dict(FEATURES, mapStarted=True),
                             ready_after=1e9, readyTimeout=5.0)
            self.assertTrue(r["done"])
            self.assertIn("the world of the play is not ready after 5 s: the dev map started not "
                          "seen in the log of the editor", r["errors"][0])
            self.assertFalse(go.sent)  # nothing sent to a world not ready

    def test_the_determinism_and_the_loads_in_the_log(self):
        """Run 6: "déterminisme : ?" and "chargements rapportés : 0", the session closed 1.3 s
        after the start. The session now ends after the line of the self-check of the plugin."""
        import ue_log
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            job = job_in(folder, fake, [EDGE], materials=False, views=False, pitchCheck=False,
                         landings=[{"place": "lisiere", "skip": None}], landingTimeout=8.0)
            job["features"] = dict(FEATURES, mapStarted=True, determinism=True)
            fake_map.MapGo(fake, [EDGE], determinism_after=25.0)  # after the landing (~5 s)
            r = drive(fake, job).result
            self.assertTrue(r["done"], r["errors"])
            scan = ue_log.scan_editor(fake.log_path.read_text(encoding="utf-8"))
            self.assertIs(scan["determinism"], True)
            self.assertTrue(scan["loadReports"])
            self.assertGreaterEqual(fake.time, 25.0)

    def test_skipped_and_unsupported(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            job = job_in(folder, fake, [EDGE], materials=False, views=False, pitchCheck=False,
                         landings=[{"place": "lisiere", "skip": "measured with the planet "
                                    "file, the run has none"}])
            job["features"] = dict(FEATURES)
            fake_map.MapGo(fake, [EDGE])
            r = drive(fake, job).result
            self.assertEqual(r["landings"][0]["skipped"],
                             "measured with the planet file, the run has none")
            self.assertFalse([c for c in calls(fake, "console") if "Map.Go" in c[1]])
        with tempfile.TemporaryDirectory() as folder:
            fake = fake_unreal.FakeUnreal(folder, features=FEATURES)
            r, _ = landings(fake, [EDGE], features=dict(FEATURES, mapGo=False))
            self.assertTrue(r["landings"][0]["unsupported"])



def landed(place, away=0.0, ok=True, who="the pawn of the game", feet=10.0, decision="land"):
    line = (f"1 s after the click: {who}, Walking, 0.00 m/s, feet at {feet:.2f} m (no water "
            f"within 3 m), {away:.1f} m from the click: " +
            ("OK" if ok else "NOT walking still above the water"))
    return {"place": place, "end": "check", "decision": decision, "seconds": 12.0,
            "radius": None, "skipped": None, "unsupported": False, "feet": [0.0, 0.0, feet],
            "check": {"who": who, "mode": "Walking", "speed": 0.0, "feet": feet,
                      "water": "no water within 3 m", "away": away, "ok": ok, "line": line}}


def watered(place, end="water", eye=2.8):
    return {"place": place, "end": end, "decision": "water" if end == "water" else "shallow",
            "seconds": 5.0, "radius": 50.0, "skipped": None, "unsupported": False,
            "check": None, "eye": [0.0, 0.0, eye], "message": "over water (no dry ground "
            "within 50 m): the view of the tools 1.8 m above the water"}


def with_landings(folder, label, entries, cores=None, **result):
    v = version(folder, label)
    v["probes"]["b"] = {"ground": 0.0, "waterLevel": 1.0, "eye": 2.8}  # a: dry, b: a river
    v["sessions"][0]["result"].update(landings=entries, **result)
    v["landingCores"] = cores or {}
    return v


def summary(folder, before, after=None):
    run = run_of(folder, before, after or before)
    if after is None:
        run["versions"] = [before]
    run.update(views=False, landings=["a", "b"], seconds=754)
    return report.write_report(run), run


class Summary(unittest.TestCase):
    def test_clean_landings(self):
        """The pawn on the dry place (k2_paroi: 38 m from the click, under the radius of 50 m),
        the view above the water of the river, the warning of the planet file no failure."""
        with tempfile.TemporaryDirectory() as folder:
            dry = dict(landed("a", away=38.0), planetWarning="was measured without the planet "
                       "file: this world has it")
            v = with_landings(folder, "version", [dry, watered("b")],
                              {"a": {"ground": 9.9, "waterLevel": None},
                               "b": {"ground": 0.0, "waterLevel": 1.0}}, ready=[6.2])
            text, run = summary(folder, v)
            self.assertEqual(text.splitlines()[1], "Aucune alerte.")
            self.assertEqual(verify.exit_code(run), 0)
            self.assertIn("a : pion du jeu posé : Walking, 0.00 m/s, pieds à 10.00 m, sol du "
                          "core 9.90 m, no water within 3 m, à 38.0 m du clic : OK (12 s)", text)
            self.assertIn("avertissement du plugin (pas une panne) : was measured without", text)
            self.assertIn("b : vue des outils au-dessus de l'eau : œil à 2.80 m, eau du core "
                          "1.00 m (+1.80 m au-dessus) : OK (5 s)", text)
            self.assertIn("Durée totale : 12 min 34 s (Unreal : version 5 min 00 s en 1 "
                          "session(s))", text)
            self.assertIn("monde de la PIE 1 prêt en 6.2 s", text)
            self.assertNotIn("vue absente", text)  # --no-views
            self.assertIn("Planche : aucune (--no-views)", text)

    def test_failures(self):
        cases = [
            ([watered("a")], "atterrissage a : lieu sec selon le core, mais la vue des outils "
             "est restée au-dessus de l'eau"),
            ([landed("a", ok=False)], "atterrissage a : 1 s after the click: the pawn of the game"
             ", Walking, 0.00 m/s, feet at 10.00 m (no water within 3 m), 0.0 m from the click: "
             "NOT walking still above the water"),
            ([landed("a", away=61.0)], "atterrissage a : pion à 61.0 m du clic, plus que le "
             "rayon de 50 m"),
            ([landed("a", who="the view of the tools")], "atterrissage a : la ligne de contrôle "
             "parle de la vue des outils"),
            ([{"place": "a", "end": "none", "seconds": 90.0}], "atterrissage a : aucune issue en "
             "90 s"),
            ([{"place": "a", "end": "notReady", "seconds": 0.2}], "atterrissage a : "
             "Terrain.Map.Go envoyé avant que le monde de la PIE soit prêt (« no world playing ») "
             ": un problème du lanceur, pas du plugin"),
            ([{"place": "a", "skipped": "no Terrain.Map.Go in this version",
               "unsupported": True}], "atterrissage a : la version n'a pas Terrain.Map.Go : "
             "atterrissage impossible"),
        ]
        for entries, expected in cases:
            with self.subTest(expected=expected[:40]), tempfile.TemporaryDirectory() as folder:
                text, run = summary(folder, with_landings(folder, "version", entries))
                self.assertIn(f"ATTENTION (version) : {expected}", text)
                self.assertEqual(verify.exit_code(run), 1)

    def test_a_session_stopped(self):
        with tempfile.TemporaryDirectory() as folder:
            v = with_landings(folder, "version", [{"place": "a", "end": "none", "seconds": 90.0}],
                              stopped={"misses": 2, "after": "a"})
            text, _ = summary(folder, v)
            self.assertIn("ATTENTION (version) : session 1 arrêtée : 2 atterrissages de suite "
                          "sans issue ou envoyés avant que le monde soit prêt (le dernier : a), "
                          "Unreal fermé", text)

    def test_an_outcome_that_differs_between_the_versions(self):
        """The world is deterministic: a wet place giving the pawn on one side and the view
        above the water on the other is a change."""
        with tempfile.TemporaryDirectory() as folder:
            before = with_landings(folder, "avant", [landed("a"), landed("b", decision="shallow")])
            after = with_landings(folder, "apres", [landed("a"), watered("b")])
            text, run = summary(folder, before, after)
            self.assertIn("ATTENTION : atterrissage b : issue différente entre avant (pion du "
                          "jeu posé près du sol sec) et après (vue des outils au-dessus de "
                          "l'eau)", text)
            self.assertNotIn("atterrissage a : issue", text)
            self.assertEqual(verify.exit_code(run), 1)

    def test_skipped_without_the_planet_file(self):
        with tempfile.TemporaryDirectory() as folder:
            skipped = {"place": "b", "end": None, "seconds": None, "unsupported": False,
                       "skipped": landings_module.SKIPPED_PLANET}
            text, run = summary(folder, with_landings(folder, "version", [landed("a"), skipped]))
            self.assertEqual(text.splitlines()[1], "Aucune alerte.")
            self.assertIn("b : sauté : mesuré avec le fichier planète, et ce run n'en a pas "
                          "(--planet no) : sauté, ce n'est pas une panne", text)

    def test_the_feet_and_the_eye_against_the_core(self):
        """Failures beyond 0.5 m for the feet and 0.05 m for the eye (real run 7: the feet
        +0.02 to +0.21 m, the eye +1.80 m exactly)."""
        cases = [  # (feet or eye, core, failure expected)
            (landed("a", feet=10.0), {"ground": 9.79, "waterLevel": None}, None),  # the K2
            (landed("a", feet=10.0), {"ground": 9.5, "waterLevel": None}, None),   # 0.5 m: in
            (landed("a", feet=10.0), {"ground": 9.4, "waterLevel": None},
             "pieds à 10.00 m, sol du core 9.40 m sous le pion (écart +0.60 m, plus de 0.50 m)"),
            (landed("a", feet=10.0), {"ground": 10.6, "waterLevel": None},
             "écart -0.60 m, plus de 0.50 m"),  # under the ground too
            (watered("b", eye=2.8), {"ground": 0.0, "waterLevel": 1.0}, None),     # +1.80 m
            (watered("b", eye=2.85), {"ground": 0.0, "waterLevel": 1.0}, None),    # 0.05 m: in
            (watered("b", eye=2.86), {"ground": 0.0, "waterLevel": 1.0},
             "œil à 2.86 m, eau du core 1.00 m (écart +0.06 m aux 1,8 m attendus, plus de "
             "0.05 m)"),
        ]
        for entry, core, failure in cases:
            with self.subTest(entry=entry["place"], core=core), \
                    tempfile.TemporaryDirectory() as folder:
                v = with_landings(folder, "version", [entry], {entry["place"]: core})
                text, run = summary(folder, v)
                if failure is None:
                    self.assertEqual(text.splitlines()[1], "Aucune alerte.")
                    self.assertEqual(verify.exit_code(run), 0)
                else:
                    self.assertIn(f"ATTENTION (version) : atterrissage {entry['place']} : ", text)
                    self.assertIn(failure, text)
                    self.assertEqual(verify.exit_code(run), 1)
                if entry.get("eye"):  # the mark of the check of the eye, as on the lines of the pawn
                    self.assertIn(f"({entry['eye'][2] - 1.0:+.2f} m au-dessus) : "
                                  f"{'OK' if failure is None else 'PAS OK'} (5 s)", text)

    def test_no_alert_above_the_warnings_of_the_log(self):
        """resume.txt said "Aucune alerte." above lines Warning of the plugin (the warning of
        the planet file of lisiere_tropicale_savane): it says how many there are below."""
        with tempfile.TemporaryDirectory() as folder:
            v = with_landings(folder, "version", [landed("a")])
            v["sessions"][0]["scan"]["terrainProblems"] = [
                "Warning: Terrain Dev Map: lisiere_tropicale_savane was measured without the "
                "planet file: this world has it", "Warning: Terrain.Pawn: the view is still "
                "loading"]
            text, run = summary(folder, v)
            self.assertEqual(text.splitlines()[1],
                             "Aucune alerte (2 avertissements du journal, ci-dessous).")
            self.assertIn("      Warning: Terrain Dev Map: lisiere_tropicale_savane was measured",
                          text)
            self.assertEqual(verify.exit_code(run), 0)
            v["sessions"][0]["scan"]["terrainProblems"] = [f"Warning: {i}" for i in range(7)]
            text, _ = summary(folder, v)
            self.assertEqual(text.splitlines()[1],
                             "Aucune alerte (7 avertissements du journal, ci-dessous).")
            self.assertIn("      … et 2 autre(s) : logs/editor_1.log", text)
            v["sessions"][0]["scan"]["terrainProblems"] = ["Warning: one"]
            text, _ = summary(folder, v)
            self.assertEqual(text.splitlines()[1],
                             "Aucune alerte (1 avertissement du journal, ci-dessous).")

    def test_the_jobs_and_the_option(self):
        import places
        found = places.load_places()
        lisiere = [p for p in found if p.name in ("k2_paroi", "lisiere_tropicale_savane")]
        self.assertEqual(landings_module.chosen("all", lisiere),
                         ["k2_paroi", "lisiere_tropicale_savane"])
        with self.assertRaises(SystemExit):
            landings_module.chosen("fleuve_plaine", lisiere)
        no_planet = landings_module.jobs(["k2_paroi", "lisiere_tropicale_savane"], lisiere,
                                         lambda p: False)
        self.assertEqual([j["skip"] for j in no_planet],  # k2_paroi measured with the planet
                         [landings_module.SKIPPED_PLANET, None])
        self.assertEqual([j["skip"] for j in landings_module.jobs(["k2_paroi"], lisiere,
                                                                  lambda p: True)], [None])


if __name__ == "__main__":
    unittest.main()
