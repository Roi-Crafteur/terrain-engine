"""The sea near a pixel in the scripts of the materials of Unreal (agent 52, PR 52a-bis), without
Unreal. The land below the sea level is not the sea: the wet rock of the shading and the depth
offset of the GPU horizon under the tiles read the sea of the core near the pixel (Sea), never the
altitude.
- The node of the sea near the pixel (temporary_terrain_hlsl.SEA_NEAR_HLSL, sea_near_hlsl), on the
  tiles and on the GPU horizon: no placeholder left, no identifier Unreal rewrites or HLSL
  reserves; the ocean read from the last float of the texels (below 0), the altitude only in its
  first line: 1 with the switch SeaNear at 0 (Terrain.SeaNear 0, the look of before) and where
  neither reader needs it (the tiles from 0 m up, the horizon from 1 m of ground and 0 m up); its
  ring from SeaNearM; the four corners of lows_sea_near_test.cpp, which replays it on the texels of
  the core.
- The wet rock (SHADE_BOTTOM): the band of agent 33 as before from 0 m up, below -1 m only near the
  sea (max(Sea, smoothstep(-1, 0, Z))); the shade node reads Sea.
- The depth offset (horizon_sea_hlsl.BEHIND_HLSL): lerp(1, saturate(Ground), Sea), saturate(Ground)
  to the bit where Sea is 1 (the coasts as before), whole where it is 0 (a dry basin).
- Both materials make the node and wire it (create_temporary_terrain_material.py,
  create_horizon_gpu_material.py), SeaNearM their parameter; SEA_NEAR_M and SEA_RING_MAX those of
  the test of the core.
- In Python, the formulas: the wet band from 0 m up the same whatever Sea, below -1 m times Sea,
  continuous; the early outs exact (where they give 1 both readers give what they would with any
  Sea); the depth factor at the sea saturate(Ground) to the bit (float32), 1 away from it; the sea
  near replayed on texels: 0 over a dry basin of texels of the land at -150 m, 1 near the ocean,
  continuous across the texels.
Usage: lows_shader_scripts_test.py <terrain_unreal/Scripts> <tests/lows_sea_near_test.cpp>
"""

import os
import pathlib
import re
import struct
import sys

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAILED:", message)


def f32(x):
    """x rounded to a float32."""
    return struct.unpack("f", struct.pack("f", x))[0]


def modules(scripts):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import unreal_horizon_blend_scripts_test as reference
    import unreal_loading_scripts_test as loading
    sys.modules.setdefault("unreal", reference.Anything())
    sys.path.insert(0, str(scripts))
    import horizon_sea_hlsl as sea_code
    import temporary_terrain_hlsl as hlsl
    import temporary_terrain_look as look
    return look, hlsl, sea_code, loading


def check_node(look, hlsl, loading, test_source):
    """The node of the sea near the pixel, on the tiles and on the GPU horizon."""
    early = {True: "if (SeaNear < 0.5 || !(Z < 0.0)) { return 1.0; }",
             False: "if (SeaNear < 0.5 || (!(Ground < 1.0) && !(Z < 0.0))) { return 1.0; }"}
    for tiles in (True, False):
        code = look.sea_near_hlsl(tiles)
        what = "tiles" if tiles else "horizon"
        for token in ("EARLY", "POS", "CELLS", "PITCH", "TEX", "RINGMAX"):
            check(re.search(rf"\b{token}\b", code) is None, f"{what}: {token} left in the node")
        check(early[tiles] in code, f"{what}: 1 with SeaNear 0 and where no reader needs it")
        check(".Load(int3((k + first) % side, 0)).a < 0.0" in code,
              f"{what}: the ocean from the last float of the texels")
        check(f"clamp(ceil(SeaNearM * 100.0 / {'SurfacePitchCm' if tiles else 'PitchCm'}), 1.0, "
              f"{float(hlsl.SEA_RING_MAX):.1f})" in code, f"{what}: the ring from SeaNearM")
        check(("if (HasSurface < 0.5) { return 1.0; }" in code) == tiles,
              f"{what}: 1 without the GPU horizon on the tiles only")
        rest = code.replace(early[tiles], "")
        check(re.search(r"\b(Z|Ground)\b", rest) is None,
              f"{what}: the altitude never tells the sea (only the early out reads it)")
        for name in loading.DECLARATION.findall(code):
            check(name not in loading.REWRITTEN and name not in loading.RESERVED,
                  f"{what}: an identifier named {name}")
    corners = ["di <= ring && dj <= ring", "di >= 1 - ring && dj <= ring",
               "di <= ring && dj >= 1 - ring", "di >= 1 - ring && dj >= 1 - ring"]
    flat = " ".join(hlsl.SEA_NEAR_HLSL.split())
    for corner in corners:
        check(corner in flat and corner in test_source, f"the corner {corner} in both")
    check(f"constexpr double kSeaNearM = {look.SEA_NEAR_M:.1f};" in test_source,
          "SEA_NEAR_M is kSeaNearM of lows_sea_near_test.cpp")
    check(f"constexpr int kRingMax = {hlsl.SEA_RING_MAX};" in test_source,
          "SEA_RING_MAX is kRingMax of lows_sea_near_test.cpp")


def check_shading(look):
    """The wet rock of the tiles and of the horizon, times Sea."""
    check("Sea" in look.shade_inputs(), "the shade node reads Sea")
    for detail in (True, False):
        code = look.shade_hlsl(detail)
        lines = [line.strip() for line in code.splitlines()]
        wet = "float wet = 1.0 - smoothstep(wetTop - 0.7, wetTop + 0.3, Z);"
        darker = "rockG *= lerp(1.0, WetDarken, wet);"
        gate = "wet *= max(Sea, smoothstep(-1.0, 0.0, Z));"
        check(wet in lines and gate in lines and darker in lines,
              f"the wet band below 0 m times Sea ({detail})")
        if wet in lines and gate in lines and darker in lines:
            check(lines.index(wet) < lines.index(gate) < lines.index(darker),
                  f"Sea after the band, before the rock ({detail})")


def check_behind(sea_code, scripts):
    code = sea_code.BEHIND_HLSL
    check("float atSea = saturate(Ground);" in code, "the depth offset reads saturate(Ground)")
    check("(atSea + (1.0 - Sea) * (1.0 - atSea))" in code,
          "the depth offset: lerp(1, saturate(Ground), Sea), exact at the sea")
    check("* saturate(Ground);" not in code, "no depth offset by the altitude alone")
    material = (scripts / "create_horizon_gpu_material.py").read_text(encoding="utf-8")
    tiles = (scripts / "create_temporary_terrain_material.py").read_text(encoding="utf-8")
    for text, what in (('"Kind", "LocalPos", "Ground", "Sea"', "the depth offset reads Sea"),
                       ('connect(sea, "", behind, "Sea")', "Sea wired to the depth offset"),
                       ('"HorizonSeaNear", look.sea_near_hlsl(False)', "the node of the horizon"),
                       ('connect(near, "", land, "Sea")', "Sea wired to the land of the horizon"),
                       ('connect(meters, "", node, "Z")', "the altitude of the land shader"),
                       ('"SeaNearM", "SeaNear"]', "SeaNearM and SeaNear read by the horizon"),
                       ('SEA_NEAR_SCALARS = {"SeaNearM": look.SEA_NEAR_M, "SeaNear": 1.0}',
                        "SeaNearM, SeaNear: parameters of the horizon"),
                       ("list(SEA_NEAR_SCALARS.items())", "SeaNearM a parameter of the horizon")):
        check(text in material, what)
    for text, what in (('"TemporaryTerrainSea", look.sea_near_hlsl(True)', "the node of the tiles"),
                       ('connect(sea_near(material, scalars, weights, meters), "", shade, "Sea")',
                        "Sea wired to the shading of the tiles"),
                       ('"SeaNearM": look.SEA_NEAR_M, "SeaNear": 1.0', "SeaNearM, SeaNear, tiles"),
                       ('"SeaNearM", "SeaNear"]', "SeaNearM and SeaNear read by the tiles")):
        check(text in tiles, what)


def lerp(a, b, t):
    return a + (b - a) * t


def sea_near(sea, u, v, ring):
    """SEA_NEAR_HLSL on a grid of texels (sea[k][i]: of the ocean)."""
    n = len(sea)
    q = (min(max(u, 0.0), n - 1 - 0.001), min(max(v, 0.0), n - 1 - 0.001))
    i0, k0 = int(q[0] // 1), int(q[1] // 1)
    corner = [0.0, 0.0, 0.0, 0.0]
    for dj in range(-ring, ring + 2):
        for di in range(-ring, ring + 2):
            i = min(max(i0 + di, 0), n - 1)
            k = min(max(k0 + dj, 0), n - 1)
            if sea[k][i]:
                for c, near in enumerate((di <= ring and dj <= ring, di >= 1 - ring and dj <= ring,
                                          di <= ring and dj >= 1 - ring,
                                          di >= 1 - ring and dj >= 1 - ring)):
                    corner[c] = max(corner[c], 1.0 if near else 0.0)
    f = (q[0] - i0, q[1] - k0)
    return lerp(lerp(corner[0], corner[1], f[0]), lerp(corner[2], corner[3], f[0]), f[1])


def check_formulas():
    """The formulas in float32: the coasts as before, a dry basin whole."""
    worst = 0
    for g in [x / 64.0 for x in range(-12800, 640)] + [0.01, 0.37, 0.999]:
        at_sea = f32(min(max(g, 0.0), 1.0))
        factor = f32(at_sea + f32(f32(1.0 - 1.0) * f32(1.0 - at_sea)))
        worst += factor != at_sea
        away = f32(at_sea + f32(f32(1.0 - 0.0) * f32(1.0 - at_sea)))
        check(abs(away - 1.0) <= 1e-7, f"the whole offset away from the sea at {g}")
    check(worst == 0, f"the depth offset at the sea not saturate(Ground) to the bit: {worst}")
    def smoothstep(e0, e1, x):
        t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
        return t * t * (3.0 - 2.0 * t)

    def gated(wet, sea, z):
        return wet * max(sea, smoothstep(-1.0, 0.0, z))

    for wet in (0.0, 0.25, 0.7, 1.0):
        for sea in (0.0, 0.4, 1.0):
            for z in (0.0, 0.5, 3.0, 4.8):
                check(gated(wet, sea, z) == wet, f"the band as before from 0 m up ({z} m)")
            for z in (-1.0, -20.0, -187.0):
                check(gated(wet, sea, z) == wet * sea, f"below -1 m only near the sea ({z} m)")
    steps = [gated(1.0, 0.0, -1.5 + k / 100.0) for k in range(200)]
    check(max(abs(a - b) for a, b in zip(steps, steps[1:])) < 0.02, "the gate continuous")
    # Where the node gives 1 early, both readers give what they would whatever Sea.
    for ground, z in ((1.0, 0.0), (1.5, 0.99), (40.0, 39.0)):
        at_sea = min(max(ground, 0.0), 1.0)
        check(all(at_sea + (1.0 - sea) * (1.0 - at_sea) == 1.0 for sea in (0.0, 0.5, 1.0)),
              f"the depth offset whole from 1 m of ground ({ground})")
        check(all(gated(0.8, sea, z) == 0.8 for sea in (0.0, 0.5)), f"the band from 0 m ({z})")
    n = 24
    land = [[False] * n for _ in range(n)]
    check(all(sea_near(land, u / 4.0, 11.3, 2) == 0.0 for u in range(4 * n)),
          "Sea 0 over texels of the land (a dry basin at -150 m: their last float at least 0)")
    shore = [[i < 8 for i in range(n)] for _ in range(n)]
    values = [sea_near(shore, u / 16.0, 9.6, 2) for u in range(16 * (n - 1))]
    check(values[0] == 1.0 and values[16 * 9] == 1.0 and values[16 * 12] == 0.0,
          "Sea 1 within the ring of the ocean, 0 beyond")
    jump = max(abs(a - b) for a, b in zip(values, values[1:]))
    check(jump <= 1.0 / 16.0 + 1e-9, f"Sea continuous across the texels: a jump of {jump}")


def main():
    scripts = pathlib.Path(sys.argv[1])
    test_source = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")
    look, hlsl, sea_code, loading = modules(scripts)
    check_node(look, hlsl, loading, " ".join(test_source.split()))
    check_shading(look)
    check_behind(sea_code, scripts)
    check_formulas()
    print("lows_shader_scripts_test:", "FAILED" if failures else "passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
