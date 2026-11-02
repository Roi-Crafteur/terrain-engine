"""The foam of the water of the tiles (water_look_hlsl.py), run here as TerrainWater computes it,
without Unreal (agent 27, phase 12A):

- The code this test runs is the code of the material: its blocks, as written here, stand in the
  HLSL of TerrainWater (FTerrainWater::Rapids and the foam of the rapids) and of TerrainFalls.
- A ramp whose water slopes 0.31: at least 35 % of foam, at most 1 % of its squares of 4 x 4 m
  nearly bare (mean foam under 0.1); the foam of 75cbf32 (the threshold of its streaks moved by
  its slow noise) refused.
- A turn of the flow (4.3 degrees across a meter, as between two segments of a river) thousands of
  meters from the origin of the noise: the streaks keep their scale there (their variation over
  the turn 1.5 times that of a straight flow at most), where the foam of 2cd328c (its noise in the
  frame of the flow about the origin of the world) breaks them into stripes (refused).
- White water: over a ramp as steep as 1.5 times the slope of full foam, the water between the
  streaks is opaque (0.85 at least): never the bed through it.
- The foam at the foot of a fall fades out to nothing on the border of its pool (KindDrop.y).
Usage: python3 unreal_water_foam_test.py <terrain_unreal/Scripts>
"""

import importlib
import math
import pathlib
import random
import sys
import types

sys.dont_write_bytecode = True
failures = []


def check(passed, what):
    if not passed:
        failures.append(what)
        print(f"FAILED: {what}")


# The blocks of the material this test runs (a change there must come here too).
RAPIDS_STRUCT_HLSL = """\
    float2 Rapids(float2 local, float2 along, float2 offset, float drift)
    {
        float2 fr = float2(dot(local, along), along.x * local.y - along.y * local.x) + offset
                  - float2(drift, 0.0);
        float patch = Noise(fr * float2(0.17, 0.21) + 5.3, 1.0);
        float2 turned = float2(0.83 * fr.x - 0.56 * fr.y, 0.56 * fr.x + 0.83 * fr.y);
        float streaks = 0.6 * Noise(fr * float2(0.45, 1.3), 1.0)
                      + 0.4 * Noise(turned * float2(0.9, 2.1) + 11.0, 1.0);
        return float2(patch, streaks);
    }"""
RAPIDS_TILE_HLSL = """\
const float RAPIDS_CELL = 4.0;
float foam = 0.0;
float white = 0.0;
BRANCH
if (slope > 0.5 * FoamFromSlope)
{
    float flowLength = length(Flow);
    float2 along = flowLength > 0.01 ? Flow / flowLength : float2(0.8, 0.6);
    float2 q = float2(0.83 * p.x + 0.56 * p.y, 0.83 * p.y - 0.56 * p.x) / RAPIDS_CELL;
    float2 lattice = floor(q);
    float2 inCell = q - lattice;
    float2 sum = float2(0.0, 0.0);
    float squares = 0.0;
    for (int k = 0; k < 4; ++k)
    {
        float2 corner = lattice + float2(k % 2, k / 2);
        float2 toward = lerp(1.0 - inCell, inCell, float2(k % 2, k / 2));
        float weight = toward.x * toward.y;
        float2 l = q - corner;
        float2 local = float2(0.83 * l.x - 0.56 * l.y, 0.56 * l.x + 0.83 * l.y) * RAPIDS_CELL;
        float2 offset = float2(Water.Hash(corner), Water.Hash(corner + 17.0)) * 256.0;
        sum += weight * (Water.Rapids(local, along, offset, FlowSpeed * Time) - 0.5);
        squares += weight * weight;
    }
    float2 rapids = 0.5 + sum / sqrt(max(squares, 1e-4));
    float from = FoamFromSlope * (0.5 + rapids.x);
    foam = saturate((slope - from) / max(FoamFullSlope - from, 1e-3));
    foam *= smoothstep(0.35, 0.75, rapids.y);
    white = 0.85 * smoothstep(FoamFullSlope, 1.5 * FoamFullSlope, slope);
}"""
WHITE_OPACITY_HLSL = ("float opacity = max(saturate(1.0 - through * (1.0 - Fresnel)), "
                      "max(foam, white));")
FALLS_FOAM_HLSL = "    opacity = rim * lerp(0.6, 0.95, n) * pool * saturate(KindDrop.y);"

RAPIDS_CELL = 4.0
RAMP_SLOPE, RAMP_FOAM_LEAST, BARE_FOAM, BARE_MOST = 0.31, 0.35, 0.1, 0.01
TURN_DEGREES, TURN_SCALE_MOST, WHITE_LEAST = 4.3, 1.5, 0.85


def frac(x):
    return x - math.floor(x)


def hash_of(c):
    """FTerrainWater::Hash: in [0, 1) for the cell c."""
    h = [frac(v * 0.1031) for v in (c[0], c[1], c[0])]
    d = h[0] * (h[1] + 33.33) + h[1] * (h[2] + 33.33) + h[2] * (h[0] + 33.33)
    h = [v + d for v in h]
    return frac((h[0] + h[1]) * h[2])


def noise(p, size):
    """FTerrainWater::Noise: value noise of cells of size meters at p."""
    q = (p[0] / size, p[1] / size)
    i = (math.floor(q[0]), math.floor(q[1]))
    f = [v - math.floor(v) for v in q]
    f = [v * v * (3.0 - 2.0 * v) for v in f]
    h00, h10 = hash_of(i), hash_of((i[0] + 1, i[1]))
    h01, h11 = hash_of((i[0], i[1] + 1)), hash_of((i[0] + 1, i[1] + 1))
    return (h00 + (h10 - h00) * f[0]) * (1.0 - f[1]) + (h01 + (h11 - h01) * f[0]) * f[1]


def smoothstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def rapids_noise(fr):
    """FTerrainWater::Rapids at fr (meters along and across the flow): (slow noise, streaks)."""
    patch = noise((fr[0] * 0.17 + 5.3, fr[1] * 0.21 + 5.3), 1.0)
    turned = (0.83 * fr[0] - 0.56 * fr[1], 0.56 * fr[0] + 0.83 * fr[1])
    streaks = (0.6 * noise((fr[0] * 0.45, fr[1] * 1.3), 1.0)
               + 0.4 * noise((turned[0] * 0.9 + 11.0, turned[1] * 2.1 + 11.0), 1.0))
    return patch, streaks


def pivoted(p, along, drift):
    """RAPIDS_TILE_HLSL: the noise of the rapids at p about the four pivots of its cell."""
    q = ((0.83 * p[0] + 0.56 * p[1]) / RAPIDS_CELL, (0.83 * p[1] - 0.56 * p[0]) / RAPIDS_CELL)
    lattice = (math.floor(q[0]), math.floor(q[1]))
    in_cell = (q[0] - lattice[0], q[1] - lattice[1])
    total = [0.0, 0.0]
    squares = 0.0
    for k in range(4):
        fx, fy = k % 2, k // 2
        corner = (lattice[0] + fx, lattice[1] + fy)
        weight = (in_cell[0] if fx else 1.0 - in_cell[0]) * (in_cell[1] if fy else 1.0 - in_cell[1])
        l = (q[0] - corner[0], q[1] - corner[1])
        local = ((0.83 * l[0] - 0.56 * l[1]) * RAPIDS_CELL,
                 (0.56 * l[0] + 0.83 * l[1]) * RAPIDS_CELL)
        offset = (hash_of(corner) * 256.0, hash_of((corner[0] + 17.0, corner[1] + 17.0)) * 256.0)
        fr = (local[0] * along[0] + local[1] * along[1] + offset[0] - drift,
              along[0] * local[1] - along[1] * local[0] + offset[1])
        n = rapids_noise(fr)
        total = [total[0] + weight * (n[0] - 0.5), total[1] + weight * (n[1] - 0.5)]
        squares += weight * weight
    scale = 1.0 / math.sqrt(max(squares, 1e-4))
    return 0.5 + total[0] * scale, 0.5 + total[1] * scale


def about_origin(p, along, drift):
    """The noise of 2cd328c: in the frame of the flow about the origin of the noise."""
    return rapids_noise((p[0] * along[0] + p[1] * along[1] - drift,
                         p[1] * along[0] - p[0] * along[1]))


def rapids_foam(p, along, slope, scalars, drift, patched):
    """The foam of the rapids at p; patched: the threshold of the streaks moved by the slow noise
    too (75cbf32, refused)."""
    patch, streaks = pivoted(p, along, drift)
    start = scalars["FoamFromSlope"] * (0.5 + patch)
    foam = min(max((slope - start) / max(scalars["FoamFullSlope"] - start, 1e-3), 0.0), 1.0)
    low, high = (0.3 + 0.2 * patch, 0.7 + 0.2 * patch) if patched else (0.35, 0.75)
    return foam * smoothstep(low, high, streaks)


def ramp_cover(scalars, patched):
    """The foam of a steep ramp: its mean and the share of its 4 x 4 m squares nearly bare, over
    6 places of 40 x 40 m (each its flow, its time), sampled every 0.5 m. Deterministic."""
    draw = random.Random(27)
    total, squares, bare = 0.0, 0, 0
    for _ in range(6):
        origin = (draw.uniform(0.0, 4096.0), draw.uniform(0.0, 4096.0))
        turn, time = draw.uniform(0.0, 2.0 * math.pi), draw.uniform(0.0, 100.0)
        along = (math.cos(turn), math.sin(turn))
        for sk in range(10):
            for si in range(10):
                share = 0.0
                for k in range(8):
                    for i in range(8):
                        p = (origin[0] + 4.0 * si + 0.5 * i + 0.25,
                             origin[1] + 4.0 * sk + 0.5 * k + 0.25)
                        share += rapids_foam(p, along, RAMP_SLOPE, scalars,
                                             scalars["FlowSpeed"] * time, patched) / 64.0
                total += share
                squares += 1
                bare += 1 if share < BARE_FOAM else 0
    return total / squares, bare / squares


def turn_scale(field):
    """How much more the streaks of field(p, along) vary across a turn of the flow (TURN_DEGREES
    over a meter, from 0.25 to 0.75 m along the line) than in a straight flow, the most over 8
    places 1,000 to 4,000 m from the origin."""
    draw = random.Random(43)
    worst = 0.0
    for _ in range(8):
        origin = (draw.uniform(1000.0, 4000.0), draw.uniform(1000.0, 4000.0))
        heading = draw.uniform(0.0, 2.0 * math.pi)
        varied, straight = 0.0, 0.0
        before_t = before_s = None
        for n in range(101):
            s = n / 100.0
            share = min(max((s - 0.25) / 0.5, 0.0), 1.0)
            angle = heading + math.radians(TURN_DEGREES) * share
            p = (origin[0] + s * math.cos(heading + 1.3), origin[1] + s * math.sin(heading + 1.3))
            turned = field(p, (math.cos(angle), math.sin(angle)), 0.0)[1]
            same = field(p, (math.cos(heading), math.sin(heading)), 0.0)[1]
            if before_t is not None:
                varied += abs(turned - before_t)
                straight += abs(same - before_s)
            before_t, before_s = turned, same
        worst = max(worst, varied / max(straight, 1e-6))
    return worst


def main(folder):
    sys.path.insert(0, str(pathlib.Path(folder)))
    sys.modules["unreal"] = types.SimpleNamespace()
    look = importlib.import_module("water_look_hlsl")
    tile = look.tile_water_hlsl()
    check(RAPIDS_STRUCT_HLSL in look.struct_hlsl(),
          "FTerrainWater::Rapids is not the code run here")
    check(RAPIDS_TILE_HLSL in tile, "the foam of the rapids is not the code run here")
    check(WHITE_OPACITY_HLSL in tile, "the white water is not the code run here")
    check(FALLS_FOAM_HLSL in look.FALLS_HLSL, "the foam of the falls does not fade by KindDrop.y")
    scalars = look.WATER_SCALARS
    mean, bare = ramp_cover(scalars, False)
    check(mean >= RAMP_FOAM_LEAST and bare <= BARE_MOST,
          f"rapids foam at slope {RAMP_SLOPE}: mean {mean:.3f}, {100 * bare:.1f}% bare squares")
    old_mean, old_bare = ramp_cover(scalars, True)
    check(old_mean < RAMP_FOAM_LEAST or old_bare > BARE_MOST,
          f"rapids foam: the checker passes the foam of 75cbf32 ({old_mean:.3f})")
    scale = turn_scale(pivoted)
    old_scale = turn_scale(about_origin)
    check(scale <= TURN_SCALE_MOST, f"rapids foam: streaks {scale:.2f} times as varied at a turn")
    check(old_scale > TURN_SCALE_MOST, f"rapids foam: the checker passes 2cd328c ({old_scale:.2f})")
    full = scalars["FoamFullSlope"]
    white = 0.85 * smoothstep(full, 1.5 * full, 1.5 * full)
    check(white >= WHITE_LEAST, f"white water: opacity {white:.2f} on the steepest rapids")
    print(f"water foam: {len(failures)} failure(s); on a ramp at slope {RAMP_SLOPE} {mean:.3f} of "
          f"foam, {100 * bare:.1f}% of it bare (75cbf32: {old_mean:.3f}, {100 * old_bare:.1f}%); "
          f"its streaks across a turn of {TURN_DEGREES} degrees {scale:.2f} times as varied as in "
          f"a straight flow (2cd328c: {old_scale:.1f}); white water opaque {white:.2f}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
