"""Pixel differences between two views of the same place (pngio.Image), the share of a view
that looks like sky (the check of the sign of the pitch), and the image of the differences."""

from dataclasses import dataclass

from pngio import Image


class SizeError(ValueError):
    """Two images of different sizes: not the same view."""


@dataclass(frozen=True)
class Difference:
    mean: float      # mean absolute difference of the channels, 0 to 255
    changed: float   # share of the pixels whose largest channel differs by more than threshold
    largest: int     # the largest difference of a channel
    threshold: int

    def text(self):
        return (f"mean {self.mean:.2f}/255, {100.0 * self.changed:.2f} % of the pixels beyond "
                f"{self.threshold}, largest {self.largest}")


def compare(a, b, threshold=24):
    if (a.width, a.height) != (b.width, b.height):
        raise SizeError(f"{a.width}x{a.height} against {b.width}x{b.height}")
    total = 0
    changed = 0
    largest = 0
    ra, rb = a.rgb, b.rgb
    for i in range(0, len(ra), 3):
        d0 = abs(ra[i] - rb[i])
        d1 = abs(ra[i + 1] - rb[i + 1])
        d2 = abs(ra[i + 2] - rb[i + 2])
        m = max(d0, d1, d2)
        total += d0 + d1 + d2
        if m > threshold:
            changed += 1
        if m > largest:
            largest = m
    pixels = max(1, a.width * a.height)
    return Difference(total / (3.0 * pixels), changed / pixels, largest, threshold)


def difference_image(a, b, gain=4):
    """|a - b| times gain, per channel, clamped: black where the two views are the same."""
    if (a.width, a.height) != (b.width, b.height):
        raise SizeError(f"{a.width}x{a.height} against {b.width}x{b.height}")
    out = bytearray(abs(x - y) * gain if abs(x - y) * gain < 256 else 255
                    for x, y in zip(a.rgb, b.rgb))
    return Image(a.width, a.height, out)


def _sky_like(rgb, i, right, below):
    r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
    if b < r or b + 8 < g or (r + g + b) < 300:
        return False
    rough = sum(abs(rgb[i + k] - rgb[right + k]) + abs(rgb[i + k] - rgb[below + k])
                for k in range(3))
    return rough < 12


def sky_share(image, step=2):
    """The share of the pixels (one in step along each axis) that look like a clear sky: bright,
    blue at least as much as red and about as much as green, smooth with their neighbors. A view
    pitched down by 30 degrees over the ground shows little of it."""
    if image.width < 2 or image.height < 2:
        return 0.0
    w = image.width
    sky = 0
    seen = 0
    for y in range(0, image.height - 1, step):
        for x in range(0, w - 1, step):
            i = 3 * (y * w + x)
            seen += 1
            if _sky_like(image.rgb, i, i + 3, i + 3 * w):
                sky += 1
    return sky / max(1, seen)


# A view pitched down by 30 degrees whose sky share is beyond this was probably pitched up.
SKY_SUSPECT = 0.5
