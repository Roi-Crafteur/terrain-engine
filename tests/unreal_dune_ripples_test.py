"""Checks the ripples of the sand of the dunes in the material of the tiles (agent 34) without
Unreal: the Sand layer (the dunes) is tiled by BombStripes, whose cells turn by 0 or 180 degrees
only (its ripples keep one direction) and whose normal is blended over the cells as the albedo
is. The tiling of the other layers (Bomb) turns the cells by quarter turns and takes the normal
of the dominant cell: at a low sun the ripples turned at once along the straight edges of its
cells, a straight line across them (b889976). A reference of both in Python, along a line across
the edges of the cells: the normal of BombStripes never jumps.
Run: python3 unreal_dune_ripples_test.py <terrain_unreal/Scripts>
"""

import math
import re
import sys
import types

failures = []


def check(passed, what):
    if not passed:
        failures.append(what)
        print(f"check failed: {what}")


class Anything(types.SimpleNamespace):
    """A stand-in for the module unreal: every attribute and call gives another one."""

    def __getattr__(self, name):
        return Anything()

    def __call__(self, *args, **kwargs):
        return Anything()


def frac(v):
    return v - math.floor(v)


def hash2(cx, cy):
    cx = math.fmod(math.fmod(cx, 4096.0) + 4096.0, 4096.0)
    cy = math.fmod(math.fmod(cy, 4096.0) + 4096.0, 4096.0)
    return (frac(math.sin(cx * 127.1 + cy * 311.7) * 43758.5453),
            frac(math.sin(cx * 269.5 + cy * 183.3) * 43758.5453))


def smoothstep(e0, e1, v):
    t = min(1.0, max(0.0, (v - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def ripple_normal(u):
    """The tangent normal of ripples along x of a period of 0.3 texture repeats."""
    return (0.5 * math.cos(2.0 * math.pi * u[0] / 0.3), 0.0)


def bomb(uv, stripes):
    q = (uv[0] / 0.7, uv[1] / 0.7)
    c = (math.floor(q[0]), math.floor(q[1]))
    low = 0.5 if stripes else 0.75
    w = (smoothstep(low, 1.0, q[0] - c[0]), smoothstep(low, 1.0, q[1] - c[1]))
    best, best_n, blended = -1.0, (0.0, 0.0), [0.0, 0.0]
    for i in range(4):
        o = (i & 1, i >> 1)
        wt = ((w[0] if o[0] else 1.0 - w[0]) * (w[1] if o[1] else 1.0 - w[1]))
        if wt <= 0.01:
            continue
        h = hash2(c[0] + o[0], c[1] + o[1])
        if stripes:
            turn = 1.0 if h[0] < 0.5 else -1.0
            cs = (turn, 0.0)
        else:
            k = math.floor(h[0] * 4.0)
            cs = ((1.0, 0.0), (0.0, 1.0), (-1.0, 0.0), (0.0, -1.0))[int(k)]
        u = (cs[0] * uv[0] - cs[1] * uv[1] + h[0] * 37.0, cs[1] * uv[0] + cs[0] * uv[1] + h[1] * 37.0)
        t = ripple_normal(u)
        n = (cs[0] * t[0] + cs[1] * t[1], -cs[1] * t[0] + cs[0] * t[1])  # transpose(R) t
        blended[0] += wt * n[0]
        blended[1] += wt * n[1]
        if wt > best:
            best, best_n = wt, n
    return tuple(blended) if stripes else best_n


def largest_jump(stripes):
    worst = 0.0
    last = None
    for s in range(20000):
        uv = (0.001 * s * 0.7 + 0.13, 0.4 + 0.0003 * s)
        n = bomb(uv, stripes)
        if last is not None:
            worst = max(worst, math.hypot(n[0] - last[0], n[1] - last[1]))
        last = n
    return worst


def main():
    sys.path.insert(0, sys.argv[1])
    sys.modules.setdefault("unreal", Anything())
    import temporary_terrain_look as look
    tiles = look.shade_hlsl(True)
    struct = look.struct_hlsl()
    sand = [i for i, layer in enumerate(look.LAYERS) if layer[0] == "Sand"]
    check(len(sand) == 1, "one Sand layer")
    if sand:
        call = re.search(r"Look\.(\w+)\(TA%d," % sand[0], tiles)
        check(call is not None and call.group(1) == "BombStripes",
              f"the Sand layer is tiled by BombStripes: {call.group(1) if call else None}")
    body = re.search(r"float3 BombStripes\(.*?\n    \}\n", struct, re.S)
    check(body is not None, "BombStripes in the struct")
    if body:
        code = body.group(0)
        check("bestUV" not in code and "nrm += wt" in code, "BombStripes blends its normal")
        check("float2(0.0, 1.0)" not in code, "BombStripes never turns by a quarter")
    step = 0.001 * 0.7
    # A ripple of 0.3 repeats changes its normal by at most 0.5 * 2 pi / 0.3 per repeat: over
    # a step of the line, about 0.01; a turn of the ripples at once jumps by up to 0.7.
    stripes, quarters = largest_jump(True), largest_jump(False)
    print(f"largest jump of the normal along the line: BombStripes {stripes:.3f}, Bomb {quarters:.3f}")
    check(stripes <= 0.05, f"the normal of BombStripes never jumps: {stripes:.3f}")
    check(quarters > 0.2, f"the reference sees the jumps of Bomb: {quarters:.3f}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
