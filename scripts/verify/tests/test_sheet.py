"""The contact sheet (sheet.py): its size, one row per view, a missing image drawn as such, the
font and planche.html."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pngio  # noqa: E402
import sheet  # noqa: E402


class Font(unittest.TestCase):
    def test_plain(self):
        self.assertEqual(sheet.plain("Écart à 3,5 %"), "ECART A 3,5 %")
        self.assertEqual(sheet.plain("a~b"), "A?B")
        self.assertEqual(sheet.text_width("abc"), 3 * sheet.GLYPH_W)

    def test_draw(self):
        image = pngio.blank(40, 20)
        sheet.draw_text(image, "1", 0, 0)
        lit = sum(1 for i in range(0, len(image.rgb), 3) if image.rgb[i] == sheet.INK[0])
        # "1" lights 10 cells of 5 x 7, each SCALE x SCALE pixels.
        self.assertEqual(lit, 10 * sheet.SCALE * sheet.SCALE)
        sheet.draw_text(image, "far beyond the edge", 30, 15)  # clipped, no error


class Sheet(unittest.TestCase):
    def test_layout(self):
        view = pngio.blank(320, 180, (10, 200, 10))
        rows = [sheet.Row("a", "mean 0.0", [view, view, view]),
                sheet.Row("b", "missing after", [view, None, None])]
        image = sheet.make_sheet(["avant", "apres", "ecarts"], rows, thumb_width=160)
        tw, th = 160, 90
        self.assertEqual(image.width, sheet.MARGIN + 3 * (tw + sheet.MARGIN))
        self.assertEqual(image.height,
                         sheet.MARGIN + sheet.BAND + 2 * (sheet.BAND + th + sheet.MARGIN))
        first = sheet.MARGIN + sheet.BAND + sheet.BAND  # top of the images of row a
        self.assertEqual(image.pixel(sheet.MARGIN + 80, first + 45), (10, 200, 10))
        second = first + th + sheet.MARGIN + sheet.BAND
        missing = image.pixel(sheet.MARGIN + (tw + sheet.MARGIN) + 100, second + 60)
        self.assertEqual(missing, sheet.MISSING)

    def test_files(self):
        view = pngio.blank(32, 18, (50, 60, 70))
        rows = [sheet.Row("chute388_bassin", "mean 1.2/255", [view, view],
                          ["avant/views/a.png", "apres/views/a.png"])]
        with tempfile.TemporaryDirectory() as folder:
            png = Path(folder) / "planche.png"
            page = Path(folder) / "planche.html"
            sheet.write_sheet(png, page, "main vs x", ["avant", "apres"], rows, thumb_width=64)
            self.assertEqual(pngio.read_png(png, use_pillow=False).width,
                             sheet.MARGIN + 2 * (64 + sheet.MARGIN))
            text = page.read_text(encoding="utf-8")
            self.assertIn("avant/views/a.png", text)
            self.assertIn("chute388_bassin", text)
            self.assertIn("<th>apres</th>", text)


if __name__ == "__main__":
    unittest.main()
