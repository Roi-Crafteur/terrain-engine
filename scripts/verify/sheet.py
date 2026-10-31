"""The contact sheet of a verification (planche avant / après): one row per view, one column per
version and one for their differences, a line of text over each row (the place and its pixel
differences), as planche.png with a small built-in font, and planche.html linking the full
images."""

import html
import unicodedata
from dataclasses import dataclass, field

import pngio

# 5 x 7 glyphs, one byte per row, the high bit of 5 on the left.
_GLYPHS = {
    "0": (0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E), "1": (0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E),
    "2": (0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F), "3": (0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E),
    "4": (0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02), "5": (0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E),
    "6": (0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E), "7": (0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08),
    "8": (0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E), "9": (0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C),
    "A": (0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11), "B": (0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E),
    "C": (0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E), "D": (0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C),
    "E": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F), "F": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10),
    "G": (0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F), "H": (0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "I": (0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E), "J": (0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C),
    "K": (0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11), "L": (0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F),
    "M": (0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11), "N": (0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11),
    "O": (0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E), "P": (0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10),
    "Q": (0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D), "R": (0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11),
    "S": (0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E), "T": (0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04),
    "U": (0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E), "V": (0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04),
    "W": (0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A), "X": (0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11),
    "Y": (0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04), "Z": (0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F),
    " ": (0, 0, 0, 0, 0, 0, 0), ".": (0, 0, 0, 0, 0, 0x0C, 0x0C), ",": (0, 0, 0, 0, 0x0C, 0x04, 0x08),
    ":": (0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0), "-": (0, 0, 0, 0x1F, 0, 0, 0),
    "_": (0, 0, 0, 0, 0, 0, 0x1F), "%": (0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03),
    "(": (0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02), ")": (0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08),
    "/": (0, 0x01, 0x02, 0x04, 0x08, 0x10, 0), "+": (0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0),
    "=": (0, 0, 0x1F, 0, 0x1F, 0, 0), "?": (0x0E, 0x11, 0x01, 0x02, 0x04, 0, 0x04),
    "!": (0x04, 0x04, 0x04, 0x04, 0, 0, 0x04), "#": (0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A),
    "<": (0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02), ">": (0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08),
    "'": (0x0C, 0x04, 0x08, 0, 0, 0, 0),
}
SCALE = 2
GLYPH_W = 6 * SCALE  # 5 columns and a space
GLYPH_H = 7 * SCALE
MARGIN = 8
BAND = GLYPH_H + 2 * 4
BACKGROUND = (32, 32, 32)
INK = (235, 235, 235)
MISSING = (90, 30, 30)


def plain(text):
    """The text in the characters of the font: accents dropped, upper case."""
    decomposed = unicodedata.normalize("NFKD", text)
    letters = "".join(c for c in decomposed if not unicodedata.combining(c)).upper()
    return "".join(c if c in _GLYPHS else "?" for c in letters)


def text_width(text):
    return GLYPH_W * len(plain(text))


def draw_text(image, text, left, top, color=INK):
    for n, char in enumerate(plain(text)):
        x0 = left + n * GLYPH_W
        for row, bits in enumerate(_GLYPHS[char]):
            for col in range(5):
                if bits & (0x10 >> col):
                    for dy in range(SCALE):
                        for dx in range(SCALE):
                            x, y = x0 + col * SCALE + dx, top + row * SCALE + dy
                            if 0 <= x < image.width and 0 <= y < image.height:
                                i = 3 * (y * image.width + x)
                                image.rgb[i:i + 3] = bytes(color)


@dataclass
class Row:
    name: str
    caption: str
    images: list                                  # pngio.Image or None, one per column
    links: list = field(default_factory=list)    # relative paths for planche.html


def _thumb_size(rows, width):
    for row in rows:
        for image in row.images:
            if image is not None:
                return width, max(1, round(width * image.height / image.width))
    return width, max(1, round(width * 9 / 16))


def make_sheet(columns, rows, thumb_width=480):
    """The sheet: the titles of the columns, then for each row its caption and its images."""
    tw, th = _thumb_size(rows, thumb_width)
    width = MARGIN + len(columns) * (tw + MARGIN)
    height = MARGIN + BAND + len(rows) * (BAND + th + MARGIN)
    sheet = pngio.blank(width, height, BACKGROUND)
    for c, title in enumerate(columns):
        draw_text(sheet, title, MARGIN + c * (tw + MARGIN), MARGIN)
    top = MARGIN + BAND
    for row in rows:
        draw_text(sheet, f"{row.name}  {row.caption}", MARGIN, top + 4)
        for c in range(len(columns)):
            image = row.images[c] if c < len(row.images) else None
            left = MARGIN + c * (tw + MARGIN)
            if image is None:
                pngio.paste(sheet, pngio.blank(tw, th, MISSING), left, top + BAND)
                draw_text(sheet, "MISSING", left + 8, top + BAND + 8)
            else:
                pngio.paste(sheet, pngio.resized(image, tw, th), left, top + BAND)
        top += BAND + th + MARGIN
    return sheet


def sheet_html(title, columns, rows):
    """planche.html: the same rows, the full images linked."""
    out = [f"<!DOCTYPE html><html><head><meta charset='utf-8'><title>{html.escape(title)}"
           "</title><style>body{background:#202020;color:#eee;font-family:sans-serif}"
           "img{width:100%}td{vertical-align:top;width:" + f"{100 // max(1, len(columns))}%"
           "}</style></head><body>", f"<h1>{html.escape(title)}</h1><table>",
           "<tr>" + "".join(f"<th>{html.escape(c)}</th>" for c in columns) + "</tr>"]
    for row in rows:
        out.append(f"<tr><td colspan='{len(columns)}'><b>{html.escape(row.name)}</b> "
                   f"{html.escape(row.caption)}</td></tr><tr>")
        for c in range(len(columns)):
            link = row.links[c] if c < len(row.links) else None
            cell = (f"<a href='{html.escape(link)}'><img src='{html.escape(link)}'></a>"
                    if link else "<i>missing</i>")
            out.append(f"<td>{cell}</td>")
        out.append("</tr>")
    out.append("</table></body></html>")
    return "\n".join(out)


def write_sheet(png_path, html_path, title, columns, rows, thumb_width=480):
    pngio.write_png(png_path, make_sheet(columns, rows, thumb_width))
    with open(html_path, "w", encoding="utf-8") as file:
        file.write(sheet_html(title, columns, rows))
