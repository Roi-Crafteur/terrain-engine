"""Checks the rock of the ergs in the material of the ground (agent 34) without Unreal: the rock
out of the sand (its buttes and mesas, the floor of an erg: HeightSample::erg, biome_blend.hpp)
is desert rock, ochre to red, not the cold grey mineral of the other rock (b889976: large
grey-blue patches with hard edges, the bare rock of the reefs). In the shaded HLSL of the tiles
and of the horizon (shade_hlsl, both): the grey rock turns to desertRock by ergFloor, after it is
made and before it is drawn; desertRock, as written there, is warm (red over green over blue,
saturated) at both ends of its hue, and the core lays no bare rock on the ergs
(heightfield_hydrology.cpp: no rock flag set by the dunes).
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
        hue = re.search(r"float3 desertRock = lerp\(float3\(([^)]*)\), float3\(([^)]*)\)", code)
        check(hue is not None, f"desertRock as a lerp of two colors (detail {detail})")
        if hue:
            ends = [tuple(float(v) for v in hue.group(k).split(",")) for k in (1, 2)]
            print(f"desert rock from {ends[0]} to {ends[1]}")
            check(all(warm(e) for e in ends), f"desert rock ochre to red: {ends}")
    hydrology = open(os.path.join(sys.argv[2], "heightfield_hydrology.cpp")).read()
    dunes = re.search(r"void Heightfield::addDunes\(.*?\n}\n", hydrology, re.S)
    check(dunes is not None and "s.rock" not in dunes.group(0),
          "the dunes lay no bare rock (no rock flag in addDunes)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
