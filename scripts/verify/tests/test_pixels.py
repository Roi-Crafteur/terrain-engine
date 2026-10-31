"""Pixel differences and the sky share (pixels.py) on made-up images."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pixels  # noqa: E402
import pngio  # noqa: E402


def textured(width, height, base=(90, 80, 60)):
    """A ground: brown, with a pattern from pixel to pixel."""
    rgb = bytearray()
    for y in range(height):
        for x in range(width):
            v = (x * 13 + y * 7) % 40
            rgb += bytes((base[0] + v, base[1] + v // 2, base[2]))
    return pngio.Image(width, height, rgb)


class Compare(unittest.TestCase):
    def test_the_same_view(self):
        a = textured(20, 10)
        d = pixels.compare(a, textured(20, 10))
        self.assertEqual((d.mean, d.changed, d.largest), (0.0, 0.0, 0))

    def test_known_differences(self):
        a = pngio.blank(10, 10, (100, 100, 100))
        b = pngio.blank(10, 10, (100, 100, 100))
        for x in range(5):  # 5 pixels of 100 changed in red by 60
            i = 3 * x
            b.rgb[i] = 160
        b.rgb[3 * 99 + 2] = 110  # one pixel changed in blue by 10, under the threshold
        d = pixels.compare(a, b, threshold=24)
        self.assertAlmostEqual(d.mean, (5 * 60 + 10) / 300.0)
        self.assertAlmostEqual(d.changed, 0.05)
        self.assertEqual(d.largest, 60)
        self.assertIn("5.00 %", d.text())

    def test_sizes(self):
        with self.assertRaises(pixels.SizeError):
            pixels.compare(pngio.blank(4, 4), pngio.blank(4, 5))
        with self.assertRaises(pixels.SizeError):
            pixels.difference_image(pngio.blank(4, 4), pngio.blank(5, 4))

    def test_difference_image(self):
        a = pngio.blank(2, 1, (10, 10, 10))
        b = pngio.Image(2, 1, bytearray([10, 10, 10, 30, 100, 0]))
        out = pixels.difference_image(a, b, gain=4)
        self.assertEqual(out.pixel(0, 0), (0, 0, 0))
        self.assertEqual(out.pixel(1, 0), (80, 255, 40))


class Sky(unittest.TestCase):
    def test_ground_and_sky(self):
        ground = textured(40, 30)
        sky = pngio.blank(40, 30, (120, 170, 230))
        self.assertLess(pixels.sky_share(ground), 0.05)
        self.assertGreater(pixels.sky_share(sky), 0.95)

    def test_a_horizon(self):
        # The upper third sky, the rest ground: about a third.
        view = textured(40, 30)
        pngio.paste(view, pngio.blank(40, 10, (130, 175, 235)), 0, 0)
        share = pixels.sky_share(view, step=1)
        self.assertGreater(share, 0.25)
        self.assertLess(share, pixels.SKY_SUSPECT)

    def test_dark_or_red_is_no_sky(self):
        self.assertEqual(pixels.sky_share(pngio.blank(10, 10, (20, 30, 60))), 0.0)
        self.assertEqual(pixels.sky_share(pngio.blank(10, 10, (230, 120, 90))), 0.0)


if __name__ == "__main__":
    unittest.main()
