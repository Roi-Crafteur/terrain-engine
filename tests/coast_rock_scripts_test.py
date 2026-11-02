"""The rock under the sea in the scripts of the materials of Unreal (agent 33), without Unreal.

A texel of the ocean of the GPU horizon holds its ground under the sea, kSeaRockCode lower over
bare rock (Bridge/horizon_textures.hpp): the materials must draw rock there, not the sand of the
shallows, and still read its ground.
- SEA_ROCK_CODE (temporary_terrain_hlsl.py) is kSeaRockCode.
- The layers of a texel (FTemporaryTerrain::Layers, as struct_hlsl writes it): the material of a
  texel of the ocean over rock is the rock (4), of another texel of the ocean the water (0); the
  codes of the land as before.
- The ground under the sea, decoded as the HLSL writes it (sea_ground_hlsl): given back from a
  texel over rock within the precision of a float, untouched from another.
- Every reader of that ground decodes it: the vertex code of the GPU horizon (HORIZON_GROUND,
  create_horizon_gpu_material.py) and the sea square under the tiles (horizon_sea_hlsl.py); no
  placeholder left.
Usage: coast_rock_scripts_test.py <terrain_unreal/Scripts> <horizon_textures.hpp>
"""

import re
import struct
import sys

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAILED:", message)


def as_float(x):
    """x rounded to a float, as the texture holds it."""
    return struct.unpack("f", struct.pack("f", x))[0]


def main():
    scripts, header = sys.argv[1], sys.argv[2]
    sys.path.insert(0, scripts)
    import horizon_sea_hlsl as sea
    import temporary_terrain_hlsl as terrain

    with open(header, encoding="utf-8") as f:
        cpp = f.read()
    found = re.search(r"kSeaRockCode\s*=\s*([0-9']+)\.0f", cpp)
    check(found is not None, "kSeaRockCode found in horizon_textures.hpp")
    code = float(found.group(1).replace("'", "")) if found else 0.0
    check(terrain.SEA_ROCK_CODE == code, f"SEA_ROCK_CODE {terrain.SEA_ROCK_CODE} == {code}")

    biomes = [((0, 0, 0, 0), (0, 0, 0, 0), (0, 0))] * 12
    struct_code = terrain.struct_hlsl(biomes)
    check("SEA_ROCK" not in struct_code, "no SEA_ROCK placeholder left in the struct")
    layer = re.search(r"int v = code < 0\.0 \? \(code <= (-?[0-9.]+) \? 4 : 0\) : "
                      r"\(int\)\(code \+ 0\.5\) % 128;", struct_code)
    check(layer is not None, "Layers picks the rock under the sea")
    threshold = float(layer.group(1)) if layer else 0.0

    def material(c):
        v = (4 if c <= threshold else 0) if c < 0.0 else int(c + 0.5) % 128
        return v % 8

    decode = re.fullmatch(r"\(x <= (-?[0-9.]+) \? x \+ ([0-9.]+) : x\)",
                          terrain.sea_ground_hlsl("x"))
    check(decode is not None, "sea_ground_hlsl decodes the rock code")
    limit, add = (float(decode.group(1)), float(decode.group(2))) if decode else (0.0, 0.0)

    def ground(c):
        return c + add if c <= limit else c

    for depth in (-0.25, -3.0, -42.5, -480.0, -4600.0, -11000.0):
        sand = as_float(depth)
        rock = as_float(depth - code)
        check(material(sand) == 0, f"the ocean at {depth} m is water")
        check(material(rock) == 4, f"the ocean over rock at {depth} m is rock")
        check(ground(sand) == sand, f"the ground at {depth} m untouched")
        check(abs(ground(rock) - depth) <= 0.01, f"the ground over rock at {depth} m given back")
    for land in (4 + 8 * 9, 2 + 8 * 4, 4 + 8 * 1 + 128):
        check(material(float(land)) == land % 128 % 8, f"the land code {land} as before")

    sea_code = sea.sea_hlsl()
    check("SEA_GROUND" not in sea_code, "no SEA_GROUND left in the sea of the horizon")
    for tap in ("t00", "t10", "t01", "t11"):
        check(terrain.sea_ground_hlsl(f"{tap}.a") in sea_code, f"the sea square decodes {tap}")

    with open(f"{scripts}/create_horizon_gpu_material.py", encoding="utf-8") as f:
        gpu = f.read()
    check("#define HORIZON_GROUND(t) ((t).a < 0.0 ? SEA_GROUND((t).a)" in gpu,
          "HORIZON_GROUND reads the ground under the sea through SEA_GROUND")
    check('VERTEX_HLSL.replace("SEA_GROUND((t).a)", terrain_hlsl.sea_ground_hlsl("(t).a"))' in gpu,
          "vertex_hlsl decodes SEA_GROUND")
    check("look.struct_hlsl() + vertex_hlsl()" in gpu, "the vertex node takes vertex_hlsl()")
    check("look.struct_hlsl() + VERTEX_HLSL" not in gpu, "no undecoded vertex code left")

    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
