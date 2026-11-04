"""Checks the rock of the ergs in the material of the ground (agent 34) without Unreal: the rock
out of the sand (its buttes and mesas, the floor of an erg: HeightSample::erg, biome_blend.hpp)
is desert rock, ochre to red, not the cold grey mineral of the other rock (b889976: large
grey-blue patches with hard edges, the bare rock of the reefs). In the shaded HLSL of the tiles
and of the horizon (shade_hlsl, both): the grey rock turns to desertRock by ergFloor, after it is
made and before it is drawn; desertRock, the local sand times its factors, is warm (red over
green over blue, saturated) at both ends of its hue and never brighter than that sand (twice as
bright, its faces lit by a low sun showed white, c1119e3), and the core lays no bare rock on the ergs
(heightfield_hydrology.cpp: no rock flag set by the dunes).
The floors of the ergs (tenth stop, f405fb7: their gravel, the material pebbles, read from afar as
grey-blue lakes in the orange sand): their Scree layer alone takes regTint by ergFloor, its mean
color then that of the local sand under a dark patina (warm, darker than the sand, never blue);
the smooth sand takes its desert tint wherever the erg lies (by ergFloor too: by the share of its
dunes only, its pale raw texture showed white as snow by the rock out of the sand).
Run: python3 unreal_erg_rock_test.py <terrain_unreal/Scripts> <terrain_core/src/generation>
"""

import os
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


def warm(rgb):
    r, g, b = rgb
    return r > g > b and (r - b) / r >= 0.5


def main():
    sys.path.insert(0, sys.argv[1])
    sys.modules.setdefault("unreal", Anything())
    import temporary_terrain_look as look
    for detail in (True, False):
        code = look.shade_hlsl(detail)
        made = code.find("float3 rockG =")
        tinted = code.find("rockG = lerp(rockG, desertRock, ergFloor);")
        drawn = code.find("float3 color = lerp(lerp(groundC, rockG, rock)")
        check(0 <= made < tinted < drawn,
              f"the rock of the ergs is tinted between its grey and the color (detail {detail})")
        check(code.find("float ergFloor") >= 0 and code.find("float ergFloor") < tinted,
              f"ergFloor is known where the rock is tinted (detail {detail})")
        hue = re.search(r"float3 desertRock = desertMean \* lerp\(float3\(([^)]*)\), float3\(([^)]*)\)",
                        code)
        check(hue is not None, f"desertRock as the local sand times a lerp of two factors (detail {detail})")
        if hue:
            names = [layer[0] for layer in look.LAYERS]
            desert = [m * t for m, t in zip(look.LAYERS[names.index("Desert")][3], look.TINTS["Desert"])]
            ends = [tuple(d * float(v) for d, v in zip(desert, hue.group(k).split(","))) for k in (1, 2)]
            lum = lambda c: 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
            print(f"desert rock from {tuple(round(v, 3) for v in ends[0])} to "
                  f"{tuple(round(v, 3) for v in ends[1])}, luminance {lum(ends[0]):.3f} to "
                  f"{lum(ends[1]):.3f} (the sand {lum(desert):.3f})")
            check(all(warm(e) for e in ends), f"desert rock ochre to red: {ends}")
            check(all(lum(e) <= lum(desert) for e in ends),
                  f"desert rock never brighter than the sand around it: {ends}")
    names = [layer[0] for layer in look.LAYERS]
    scree = names.index("Scree")
    for detail in (True, False):
        code = look.shade_hlsl(detail)
        start = code.find(f"BRANCH if (g[{scree}] > 0.01")
        end = code.find("BRANCH if (g[", start + 10)
        end = end if end >= 0 else len(code)
        check(code.count("c *= regTint;") == 1 and start < code.find("c *= regTint;") < end,
              f"the Scree layer alone takes regTint (detail {detail})")
        check("max(sandLock, ergFloor)" in code,
              f"the sand of the erg tinted wherever the erg lies (detail {detail})")
        shade = re.search(r"desertMean \* ([0-9.]+) / max\(screeMean", code)
        check(shade is not None, f"regTint from the desert mean (detail {detail})")
    if shade:
        k = float(shade.group(1))
        desert = [m * t for m, t in zip(look.LAYERS[names.index("Desert")][3], look.TINTS["Desert"])]
        reg = [k * d for d in desert]
        grey = [m * t for m, t in zip(look.LAYERS[scree][3], look.TINTS["Scree"])]
        print(f"gravel of an erg {tuple(round(v, 3) for v in reg)} (the scree elsewhere "
              f"{tuple(round(v, 3) for v in grey)}, the desert sand {tuple(round(v, 3) for v in desert)})")
        check(warm(reg) and sum(reg) < 0.7 * sum(desert), f"the gravel of an erg a dark warm reg: {reg}")
    hydrology = open(os.path.join(sys.argv[2], "heightfield_hydrology.cpp")).read()
    dunes = re.search(r"void Heightfield::addDunes\(.*?\n}\n", hydrology, re.S)
    check(dunes is not None and "s.rock" not in dunes.group(0),
          "the dunes lay no bare rock (no rock flag in addDunes)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
