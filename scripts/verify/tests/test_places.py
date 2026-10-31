"""The places of the dev tools (places.py): the shared file of the repository is valid, the format
is checked, the conversions to Unreal are those of the bridge."""

import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import places  # noqa: E402

ENTRY = {"name": "a", "x": 10, "z": -20, "heading": 90, "pitch": -5, "eye": 2,
         "eyeMode": "ground", "planet": True, "note": "n"}


def doc(*entries):
    return {"version": 1, "places": list(entries)}


class SharedFile(unittest.TestCase):
    def test_the_file_of_the_repository(self):
        found = places.load_places()
        names = [p.name for p in found]
        self.assertIn("chute388_bassin", names)
        for name in ("chute388_levre", "chute388_mer", "k2_paroi", "falaise_cotiere",
                     "montagnes_plongent", "cote_froide", "plage_origine", "fleuve_plaine",
                     "lac_montagne", "lisiere_tropicale_savane"):
            self.assertIn(name, names)
        basin = places.select(found, ["chute388_bassin"])[0]
        self.assertEqual((basin.x, basin.z, basin.heading, basin.pitch, basin.eye),
                         (-6185104.0, 155924.0, 227.0, 30.0, 182.03))
        self.assertEqual(basin.eye_mode, "altitude")
        self.assertTrue(basin.planet)


class Format(unittest.TestCase):
    def test_a_valid_place(self):
        (p,) = places.parse_places(doc(ENTRY))
        self.assertEqual(p, places.Place("a", 10.0, -20.0, 90.0, -5.0, 2.0, "ground", True, "n"))

    def test_rejected(self):
        bad = [
            {"version": 2, "places": []},
            {"places": []},
            {"version": 1, "places": {}},
            doc({**ENTRY, "eyeMode": "above"}),
            doc({**ENTRY, "x": "far"}),
            doc({**ENTRY, "z": float("nan")}),
            doc({**ENTRY, "heading": True}),
            doc({**ENTRY, "pitch": 95}),
            doc({**ENTRY, "planet": "yes"}),
            doc({**ENTRY, "name": "two words"}),
            doc({**ENTRY, "name": ""}),
            doc({**ENTRY, "note": 3}),
            doc(ENTRY, ENTRY),
            doc("not an object"),
        ]
        for data in bad:
            with self.subTest(data=data), self.assertRaises(places.PlacesError):
                places.parse_places(data)

    def test_not_json(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "places.json"
            path.write_text("{ version: 1 }", encoding="utf-8")
            with self.assertRaises(places.PlacesError):
                places.load_places(path)
            path.write_text(json.dumps(doc(ENTRY)), encoding="utf-8")
            self.assertEqual(len(places.load_places(path)), 1)


class Select(unittest.TestCase):
    def setUp(self):
        self.places = places.parse_places(doc({**ENTRY, "name": "a"}, {**ENTRY, "name": "b"}))

    def test_all_and_order(self):
        self.assertEqual([p.name for p in places.select(self.places, "all")], ["a", "b"])
        self.assertEqual([p.name for p in places.select(self.places, [])], ["a", "b"])
        self.assertEqual([p.name for p in places.select(self.places, "b,a")], ["b", "a"])

    def test_unknown(self):
        with self.assertRaises(places.PlacesError) as caught:
            places.select(self.places, ["a", "c"])
        self.assertIn("c", str(caught.exception))


class Unreal(unittest.TestCase):
    def test_yaw_is_heading_minus_90(self):
        for heading, yaw in ((0, -90), (90, 0), (180, 90), (227, 137), (270, 180), (359, -91),
                             (-90, 180), (450, 0)):
            self.assertAlmostEqual(places.unreal_yaw(heading), yaw)

    def test_forward(self):
        north = places.forward(0)
        east = places.forward(90)
        self.assertAlmostEqual(north[0], 0.0)
        self.assertAlmostEqual(north[1], -1.0)
        self.assertAlmostEqual(east[0], 1.0)
        self.assertAlmostEqual(east[1], 0.0, places=12)

    def test_eye(self):
        p = places.parse_places(doc(ENTRY))[0]
        self.assertEqual(places.eye_altitude(p, 100.0), 102.0)
        self.assertEqual(places.eye_altitude(p, 100.0, 104.5), 106.5)
        self.assertEqual(places.eye_altitude(p, 100.0, 90.0), 102.0)
        q = places.parse_places(doc({**ENTRY, "eyeMode": "altitude", "eye": 182.03}))[0]
        self.assertEqual(places.eye_altitude(q, 178.818, 180.028), 182.03)

    def test_frame(self):
        u = places.core_to_unreal(-6185104.0, 182.03, 155924.0, -6185104.0, 178.818, 155924.0)
        self.assertEqual(u[:2], (0.0, 0.0))
        self.assertAlmostEqual(u[2], 321.2, places=6)
        back = places.unreal_to_core(1234.0, -567.0, 89.0, 10.0, 20.0, 30.0)
        self.assertEqual(back, (10.0 + 12.34, 20.0 + 0.89, 30.0 - 5.67))
        self.assertEqual(places.core_to_unreal(*back, 10.0, 20.0, 30.0)[0], 1234.0)

    def test_tilt(self):
        self.assertAlmostEqual(places.tilt_to_pitch(1.0), 45.0)
        self.assertAlmostEqual(places.tilt_to_pitch(-1.0), -45.0)
        self.assertAlmostEqual(places.tilt_to_pitch(0.21), math.degrees(math.atan(0.21)))


if __name__ == "__main__":
    unittest.main()
