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
- The wet rock at the sea (temporary_terrain_hlsl.py): darker up to about WetBandM meters (3 m),
  its upper edge ragged by noises of the world position (between half and one and a half the
  band), in the shading of the tiles and of the GPU horizon alike (look.shade_hlsl): no step at
  512 m.
- The blends of the GPU horizon (agent 29: its colours come from them, Layers only where a texel
  holds none): the blend the core gives a texel of the ocean over rock (the ocean, all rock:
  coast_horizon_rock_test) draws rock and no sand of the shallows through FinishBlend
  (blend_layers_hlsl.py, by the reference of unreal_horizon_blend_scripts_test, whose terms of the
  sand, of the scree and of the rock are checked in the HLSL), a texel of the ocean over water
  the sand of the shallows.
Usage: coast_rock_scripts_test.py <terrain_unreal/Scripts> <horizon_textures.hpp>
"""

import os
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


def check_blends(struct_code):
    """FinishBlend draws rock for the blend of a texel of the ocean over rock."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import unreal_horizon_blend_scripts_test as reference
    sys.modules.setdefault("unreal", reference.Anything())
    import temporary_terrain_look as look

    for line in ("a = keep * A + 0.3 * soil * A + wm0.x * ocean * BA[0];",
                 "(wm1.x + wm1.w) * float4(0.0, 0.0, 0.0, 1.0);",
                 "c.y = lerp(c.y, 1.0, wm1.x);"):
        check(line in struct_code, f"FinishBlend holds {line}")
    ocean, rock, water = 0, 4, 0
    over_rock = reference.channels((ocean, ocean, ocean), (255, 0), (rock, rock, rock), (255, 0))
    over_water = reference.channels((ocean, ocean, ocean), (255, 0), (water, water, water),
                                    (255, 0))
    for alpha in (0.0, 0.5):  # this sample alone, and halfway through the morph of its level
        la, lb, lc, _, _ = reference.blend_texels_layers(
            (over_rock, over_rock, over_rock), (1.0 - alpha, 0.5 * alpha, 0.5 * alpha),
            look.BIOMES)
        check(all(abs(x) < 1e-9 for x in la), f"no layer of the biomes over rock ({alpha}): {la}")
        # Layers b: (dirt, sand, desert sand, scree): the scree alone, no sand of the shallows.
        check(all(abs(x - y) < 1e-9 for x, y in zip(lb, (0.0, 0.0, 0.0, 1.0))),
              f"the scree alone over rock, no sand ({alpha}): {lb}")
        check(abs(lc[1] - 1.0) < 1e-9, f"the rock over rock ({alpha}): {lc}")
    _, lb, lc, _, _ = reference.blend_texels_layers((over_water,), (1.0,), look.BIOMES)
    check(abs(lb[1] - 1.0) < 1e-9 and lc[1] < 1.0, f"the sand of the shallows over water: {lb}, {lc}")


def check_wet_band(scripts):
    """The wet rock at the sea, in the shading of the tiles and of the GPU horizon alike."""
    import temporary_terrain_look as look

    for detail in (False, True):
        code = look.shade_hlsl(detail)
        check("float wetTop = WetBandM * (0.5 + 0.7 * Look.Noise(P + 97.0, 12.0) "
              "+ 0.3 * Look.Noise(P - 41.0, 3.0));" in code, f"the ragged wet edge ({detail})")
        check("float wet = 1.0 - smoothstep(wetTop - 0.7, wetTop + 0.3, Z);" in code,
              f"the wet band by the altitude ({detail})")
        check("rockG *= lerp(1.0, WetDarken, wet);" in code, f"the wet rock darker ({detail})")
    check("WetBandM" in look.shade_inputs() and "WetDarken" in look.shade_inputs(),
          "the wet band read by the shading")
    band, darken = look.SCALARS["WetBandM"], look.SCALARS["WetDarken"]
    check(band == 3.0 and 0.0 < darken < 1.0, f"a wet band of 3 m, darker: {band}, {darken}")
    # Both materials shade the land with it.
    for name, call in (("create_horizon_gpu_material.py", "look.shade_hlsl(False)"),
                       ("create_temporary_terrain_material.py", "look.shade_hlsl(")):
        with open(f"{scripts}/{name}", encoding="utf-8") as f:
            check(call in f.read(), f"{name} shades the land with the look")

    def smoothstep(a, b, x):
        t = min(max((x - a) / (b - a), 0.0), 1.0)
        return t * t * (3.0 - 2.0 * t)

    def wet(z, n12, n3):
        top = band * (0.5 + 0.7 * n12 + 0.3 * n3)
        return 1.0 - smoothstep(top - 0.7, top + 0.3, z)

    tops = []
    for n12 in (0.0, 0.25, 0.5, 0.75, 1.0):
        for n3 in (0.0, 0.5, 1.0):
            check(wet(0.0, n12, n3) == 1.0, "the rock wet at the sea level")
            check(wet(1.5 * band + 0.3, n12, n3) == 0.0, "dry above one and a half the band")
            tops.append(next(z / 10.0 for z in range(0, 100) if wet(z / 10.0, n12, n3) < 0.5))
    check(max(tops) - min(tops) >= band, f"a ragged upper edge: {min(tops)} to {max(tops)} m")


def check_bands(scripts, struct_code):
    """The bands of the rock (agent 33): Look.Bands in the shading of the tiles and of the GPU
    horizon, the same numbers as its copy in the tests (coast_band_support.hpp: bandsOf), fed by
    the UV channels 1 to 3 of the tiles and by the band texels of the horizon."""
    check("float Bands(float4 B, float2 C)" in struct_code, "Look.Bands is declared")
    import temporary_terrain_hlsl as terrain_hlsl
    check("rockG *= Look.Bands(B, C);" in terrain_hlsl.SHADE_BOTTOM, "the rock takes its bands")
    import temporary_terrain_look as look
    check("B" in look.shade_inputs() and "C" in look.shade_inputs(), "the shade node reads B, C")
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "coast_band_support.hpp"),
              encoding="utf-8") as f:
        copy = f.read()
    for hlsl, cpp in (("0.24 * (frac(sin(k * 12.9898 + 4.1) * 43758.5453) - 0.5)",
                       "0.24 * (fract(std::sin(k * 12.9898 + 4.1) * 43758.5453) - 0.5)"),
                      ("1.0 - 0.32 * (1.0 - smoothstep(0.0, max(1.5 * w, 0.03) + 0.06, f))",
                       "1.0 - 0.32 * (1.0 - smoothstep(0.0, std::max(1.5 * w, 0.03) + 0.06,"),
                      ("saturate((0.35 - w) / 0.2)", "std::clamp((0.35 - w) / 0.2, 0.0, 1.0)"),
                      ("1.0 - 0.12 * (1.0 - smoothstep(0.0, max(1.5 * lw, 0.05) + 0.1, frac(l)))",
                       "1.0 - 0.12 *"),
                      ("lerp(1.0, 0.55, notch)", "(1.0 - 0.45 * notch)"),
                      ("lerp(1.0, 0.7, cap)", "(1.0 - 0.3 * cap)"),
                      ("smoothstep(-0.4, 0.2, B.z) * (1.0 - smoothstep(B.w - 0.2, B.w + 0.4, B.z))",
                       "smoothstep(-0.4, 0.2, n) * (1.0 - smoothstep(nh - 0.2, nh + 0.4, n))")):
        check(hlsl in struct_code, f"Look.Bands holds {hlsl}")
        check(cpp in copy, f"bandsOf holds {cpp}")
    with open(f"{scripts}/create_temporary_terrain_material.py", encoding="utf-8") as f:
        tiles = f.read()
    check('set_editor_property("coordinate_index", index)' in tiles and "(1, 2, 3)" in tiles,
          "the tiles read their bands from the UV channels 1 to 3")
    with open(f"{scripts}/create_horizon_gpu_material.py", encoding="utf-8") as f:
        gpu = f.read()
    check("BandsOut = BandTex.Load(int3((ik + first) % side, 0));" in gpu and
          '"HorizonBands"' in gpu, "the horizon reads the band texel of its sample")


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

    check_blends(terrain.struct_hlsl(biomes))
    check_wet_band(scripts)
    check_bands(scripts, terrain.struct_hlsl(biomes))
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
