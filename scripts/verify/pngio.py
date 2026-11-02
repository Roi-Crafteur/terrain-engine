"""PNG images in pure Python (the standard library only): read 8- and 16-bit grayscale, RGB and
their alpha versions (non-interlaced), write 8-bit RGB. Unreal writes its screenshots as such
PNG files. Pillow, when installed, reads faster; it is never needed.

An Image holds width x height RGB pixels, row by row, in a bytearray of 3 bytes per pixel.
"""

import struct
import zlib
from dataclasses import dataclass, field

SIGNATURE = b"\x89PNG\r\n\x1a\n"
_CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}  # color type -> samples per pixel


class PngError(ValueError):
    """A file this reader cannot read."""


@dataclass
class Image:
    width: int
    height: int
    rgb: bytearray = field(repr=False)

    def __post_init__(self):
        if len(self.rgb) != 3 * self.width * self.height:
            raise ValueError("an image holds 3 bytes per pixel")

    def pixel(self, x, y):
        i = 3 * (y * self.width + x)
        return tuple(self.rgb[i:i + 3])

    def row(self, y):
        start = 3 * self.width * y
        return self.rgb[start:start + 3 * self.width]


def blank(width, height, color=(0, 0, 0)):
    return Image(width, height, bytearray(bytes(color) * (width * height)))


def _chunks(data):
    if data[:8] != SIGNATURE:
        raise PngError("not a PNG file")
    at = 8
    while at + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if len(body) != length:
            raise PngError("truncated PNG file")
        yield kind, body
        at += 12 + length


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(kind, line, prior, bpp):
    """One scanline without its filter (kind 0 to 4)."""
    if kind == 0:
        return bytearray(line)
    out = bytearray(line)
    n = len(out)
    if kind == 2:
        return bytearray((a + b) & 255 for a, b in zip(line, prior))
    for i in range(n):
        left = out[i - bpp] if i >= bpp else 0
        if kind == 1:
            out[i] = (out[i] + left) & 255
        elif kind == 3:
            out[i] = (out[i] + ((left + prior[i]) >> 1)) & 255
        elif kind == 4:
            up_left = prior[i - bpp] if i >= bpp else 0
            out[i] = (out[i] + _paeth(left, prior[i], up_left)) & 255
        else:
            raise PngError(f"unknown PNG filter {kind}")
    return out


def _to_rgb(line, channels, depth):
    """The RGB bytes of an unfiltered scanline."""
    if depth == 16:
        line = line[0::2]  # the high byte of each sample
    if channels == 3:
        return bytes(line)
    out = bytearray()
    if channels == 4:
        for i in range(0, len(line), 4):
            out += line[i:i + 3]
    else:  # gray, with or without alpha
        for i in range(0, len(line), channels):
            out += bytes((line[i],)) * 3
    return bytes(out)


def _decode(data):
    header = None
    compressed = bytearray()
    for kind, body in _chunks(data):
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    if header is None:
        raise PngError("no IHDR chunk")
    width, height, depth, color, _, _, interlace = header
    if color not in _CHANNELS or depth not in (8, 16) or interlace != 0:
        raise PngError(f"unsupported PNG (color type {color}, depth {depth}, interlace "
                       f"{interlace})")
    channels = _CHANNELS[color]
    bpp = channels * depth // 8
    stride = width * bpp
    try:
        raw = zlib.decompress(bytes(compressed))
    except zlib.error as error:
        raise PngError(f"damaged image data ({error})") from error
    if len(raw) < height * (stride + 1):
        raise PngError("truncated image data")
    rgb = bytearray()
    prior = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        line = _unfilter(raw[start], raw[start + 1:start + 1 + stride], prior, bpp)
        rgb += _to_rgb(line, channels, depth)
        prior = line
    return Image(width, height, rgb)


def _read_with_pillow(path):
    try:
        from PIL import Image as PilImage  # optional, faster
    except ImportError:
        return None
    with PilImage.open(path) as picture:
        picture = picture.convert("RGB")
        return Image(picture.width, picture.height, bytearray(picture.tobytes()))


def read_png(path, use_pillow=True):
    if use_pillow:
        image = _read_with_pillow(path)
        if image is not None:
            return image
    with open(path, "rb") as file:
        return _decode(file.read())


def encode_png(image):
    """The bytes of an 8-bit RGB PNG of the image (no filter)."""
    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))
    stride = 3 * image.width
    raw = bytearray()
    for y in range(image.height):
        raw.append(0)
        raw += image.rgb[y * stride:(y + 1) * stride]
    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 2, 0, 0, 0)
    return (SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) +
            chunk(b"IEND", b""))


def write_png(path, image):
    with open(path, "wb") as file:
        file.write(encode_png(image))


def resized(image, width, height):
    """The image at another size, each pixel the nearest one of the source."""
    out = bytearray()
    xs = [min(image.width - 1, (2 * x + 1) * image.width // (2 * width)) for x in range(width)]
    for y in range(height):
        sy = min(image.height - 1, (2 * y + 1) * image.height // (2 * height))
        row = image.row(sy)
        for sx in xs:
            out += row[3 * sx:3 * sx + 3]
    return Image(width, height, out)


def paste(target, source, left, top):
    """source drawn into target with its corner at (left, top), clipped."""
    for y in range(source.height):
        ty = top + y
        if not 0 <= ty < target.height:
            continue
        x0 = max(0, -left)
        x1 = min(source.width, target.width - left)
        if x1 <= x0:
            continue
        src = 3 * (y * source.width + x0)
        dst = 3 * (ty * target.width + left + x0)
        target.rgb[dst:dst + 3 * (x1 - x0)] = source.rgb[src:src + 3 * (x1 - x0)]
