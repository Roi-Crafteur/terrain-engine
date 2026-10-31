"""PNG in pure Python (pngio.py): every filter, gray, RGB and alpha, 8 and 16 bits, against the
same pixels written by this test, and against Pillow when it is installed."""

import random
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pngio  # noqa: E402


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def filtered(kind, line, prior, bpp):
    out = bytearray()
    for i, value in enumerate(line):
        left = line[i - bpp] if i >= bpp else 0
        up_left = prior[i - bpp] if i >= bpp else 0
        predictor = [0, left, prior[i], (left + prior[i]) >> 1, paeth(left, prior[i], up_left)]
        out.append((value - predictor[kind]) & 255)
    return out


def encode(width, height, samples, color, depth, kinds):
    """A PNG of these raw samples (bytes of the rows, unfiltered), row y filtered by kinds[y]."""
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color]
    bpp = channels * depth // 8
    stride = width * bpp
    raw = bytearray()
    prior = bytearray(stride)
    for y in range(height):
        line = samples[y * stride:(y + 1) * stride]
        kind = kinds[y % len(kinds)]
        raw.append(kind)
        raw += filtered(kind, line, prior, bpp)
        prior = line

    def chunk(name, body):
        return struct.pack(">I", len(body)) + name + body + struct.pack(">I", zlib.crc32(name + body))
    head = struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, 0)
    return (pngio.SIGNATURE + chunk(b"IHDR", head) + chunk(b"IDAT", zlib.compress(bytes(raw))) +
            chunk(b"IEND", b""))


def decode(data):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "a.png"
        path.write_bytes(data)
        return pngio.read_png(path, use_pillow=False)


class Read(unittest.TestCase):
    def setUp(self):
        self.rng = random.Random(37)

    def test_rgb_every_filter(self):
        w, h = 13, 10
        samples = bytes(self.rng.randrange(256) for _ in range(w * h * 3))
        image = decode(encode(w, h, samples, 2, 8, [0, 1, 2, 3, 4]))
        self.assertEqual((image.width, image.height), (w, h))
        self.assertEqual(bytes(image.rgb), samples)

    def test_rgba_drops_alpha(self):
        w, h = 7, 6
        samples = bytes(self.rng.randrange(256) for _ in range(w * h * 4))
        image = decode(encode(w, h, samples, 6, 8, [4, 3, 1]))
        expected = b"".join(samples[i:i + 3] for i in range(0, len(samples), 4))
        self.assertEqual(bytes(image.rgb), expected)

    def test_gray_and_16_bits(self):
        w, h = 5, 4
        gray = bytes(self.rng.randrange(256) for _ in range(w * h))
        image = decode(encode(w, h, gray, 0, 8, [2, 4]))
        self.assertEqual(bytes(image.rgb), b"".join(bytes((g, g, g)) for g in gray))
        deep = bytes(self.rng.randrange(256) for _ in range(w * h * 6))
        image = decode(encode(w, h, deep, 2, 16, [1, 4]))
        self.assertEqual(bytes(image.rgb), deep[0::2])

    def test_refused(self):
        with self.assertRaises(pngio.PngError):
            decode(b"GIF89a")
        with self.assertRaises(pngio.PngError):
            decode(encode(2, 2, bytes(16), 6, 8, [0])[:-30])

    def test_write_and_read_back(self):
        image = pngio.Image(4, 3, bytearray(self.rng.randrange(256) for _ in range(36)))
        back = decode(pngio.encode_png(image))
        self.assertEqual(back, image)

    def test_against_pillow(self):
        try:
            from PIL import Image as PilImage
        except ImportError:
            self.skipTest("Pillow is not installed")
        w, h = 64, 40
        samples = bytes(self.rng.randrange(256) for _ in range(w * h * 3))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "p.png"
            PilImage.frombytes("RGB", (w, h), samples).save(path, optimize=True)
            self.assertEqual(bytes(pngio.read_png(path, use_pillow=False).rgb), samples)
            self.assertEqual(bytes(pngio.read_png(path, use_pillow=True).rgb), samples)


class Edit(unittest.TestCase):
    def test_resized_and_paste(self):
        image = pngio.Image(2, 2, bytearray([10, 0, 0, 20, 0, 0, 30, 0, 0, 40, 0, 0]))
        big = pngio.resized(image, 4, 4)
        self.assertEqual(big.pixel(0, 0), (10, 0, 0))
        self.assertEqual(big.pixel(3, 0), (20, 0, 0))
        self.assertEqual(big.pixel(0, 3), (30, 0, 0))
        self.assertEqual(big.pixel(3, 3), (40, 0, 0))
        canvas = pngio.blank(3, 3, (1, 2, 3))
        pngio.paste(canvas, image, 2, -1)
        self.assertEqual(canvas.pixel(2, 0), (30, 0, 0))
        self.assertEqual(canvas.pixel(1, 0), (1, 2, 3))
        self.assertEqual(canvas.pixel(2, 1), (1, 2, 3))


if __name__ == "__main__":
    unittest.main()
