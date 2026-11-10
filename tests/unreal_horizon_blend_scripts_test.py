"""Checks the HLSL of the surface blends of the horizon (agent 29) without Unreal:

- the blend functions (Scripts/blend_layers_hlsl.py) pasted into the helper struct, and the
  vertex shaders of the GPU horizon and of the tiles that call them: no identifier Unreal rewrites
  or HLSL reserves (half, fixed, sample, point, linear...: the lists of
  unreal_loading_scripts_test), no name declared twice in a scope or shadowing one around it, no
  member function of the struct twice; the inputs and parameters they read declared; the blend
  textures read without filtering (Load), each texel decoded before it is added;
- a reference of AddBlend, FinishBlend and TexelBlend in Python, line for line: the channels of
  Bridge/horizon_blend_texels.hpp decoded back to their ids and weights, and the morph halfway
  between two texels of different layers (and the bilinear mean of 4 under the tiles) keeps
  weights summing to 1, biomes and materials, and ground layers summing to 1.
Run: python3 unreal_horizon_blend_scripts_test.py <terrain_unreal>
"""

import ast
import math
import struct
import pathlib
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


def constants(path, names):
    """Module-level constants of a script that runs at import (its HLSL, its parameters)."""
    values = {"math": math, "unreal": Anything(), "detail": Anything()}
    for node in ast.parse(path.read_text(encoding="utf-8")).body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name in names:
                values[name] = eval(compile(ast.Expression(node.value), str(path), "eval"), values)
    return values


def strip(code):
    return re.sub(r"//[^\n]*", "", code)


def scoped_declarations(code, declaration, balanced):
    """(name, scopes) of every declaration of code, scopes the path of the braces around it. A
    member of the struct (balanced False) may end with the brace closing the struct."""
    found = []
    path = [0]
    counter = 0
    position = 0
    for match in re.finditer(r"[{}]|" + declaration.pattern, code):
        token = match.group(0)
        if token == "{":
            counter += 1
            path.append(counter)
        elif token == "}":
            if len(path) > 1 or balanced:
                path.pop()
        else:
            found.append((match.group(1), tuple(path)))
        position = match.end()
    check(len(path) == 1 and position > 0, "HLSL braces unbalanced")
    return found


def shadowed(code, declaration, balanced=True):
    """The names declared twice in one scope or in a scope inside another declaring it (for a
    member of the struct: its parameters, then its locals), and the declarations scanned."""
    seen = []
    twice = []
    for name, scopes in scoped_declarations(code, declaration, balanced):
        twice += [name for other, around in seen
                  if other == name and scopes[: len(around)] == around]
        seen.append((name, scopes))
    return twice, len(seen)


def check_scopes(code, declaration, what, balanced=True):
    twice, count = shadowed(code, declaration, balanced)
    check(not twice, f"{what}: declared twice or shadowed: {twice}")
    return count


def check_code(look_hlsl, loading, scripts):
    """Identifiers, scopes, inputs and reads of the blend code and the vertex shaders."""
    declaration = loading.DECLARATION
    blend = (scripts / "blend_layers_hlsl.py").read_text(encoding="utf-8")
    gpu = constants(scripts / "create_horizon_gpu_material.py", {"VERTEX_HLSL", "SCALARS"})
    tiles = constants(scripts / "create_temporary_terrain_material.py",
                      {"VERTEX_HLSL", "BLEND_INPUTS"})
    tiles_source = (scripts / "create_temporary_terrain_material.py").read_text(encoding="utf-8")
    struct = strip(look_hlsl)
    members = re.findall(r"\n    (?:bool|void|float[1-4]?|uint[1-4]?|int) ([A-Z][A-Za-z0-9]*)\(", struct)
    check(len(members) == len(set(members)), f"a member of the struct twice: {members}")
    for name in ("AddBlend", "FinishBlend", "BlendLayers", "TexelBlend", "TexelWater",
                 "BlendTexelsLayers"):
        check(members.count(name) == 1, f"the struct has no {name}")
    declared = 0
    bodies = re.split(r"\n    (?=(?:bool|void|float[1-4]?|uint[1-4]?|int) [A-Z][A-Za-z0-9]*\()", struct)
    for body in bodies[1:]:
        declared += check_scopes(body, declaration, f"struct member {body.split('(')[0]}", False)
    for what, code in (("horizon vertex", gpu["VERTEX_HLSL"]),
                       ("tiles vertex", tiles["VERTEX_HLSL"])):
        declared += check_scopes(strip(code), declaration, what)
        for name in declaration.findall(strip(code)):
            check(name not in loading.REWRITTEN and name not in loading.RESERVED,
                  f"{what}: an identifier named {name}")
    for name in declaration.findall(strip(blend)):
        check(name not in loading.REWRITTEN and name not in loading.RESERVED,
              f"blend_layers_hlsl.py: an identifier named {name}")
    # The inputs read and how.
    material = (scripts / "create_horizon_gpu_material.py").read_text(encoding="utf-8")
    check('"Blend", "Local"' in material and '"HorizonBlend"' in material,
          "the horizon material declares no Blend input bound to HorizonBlend")
    check("SurfaceBlends" in gpu["SCALARS"], "the horizon material has no SurfaceBlends")
    check('"SurfaceBlends": 1.0' in tiles_source
          and '"HasSurface", "SurfaceBlends"]' in tiles_source,
          "the tile material has no SurfaceBlends input")
    check('"SBlend"' in (scripts / "create_temporary_terrain_material.py").read_text("utf-8"),
          "the tile material declares no SBlend input")
    for what, code, texture in (("horizon", gpu["VERTEX_HLSL"], "Blend"),
                                ("tiles", tiles["VERTEX_HLSL"], "SBlend")):
        check(f"{texture}.Load(" in code and f"{texture}.Sample" not in code,
              f"{what}: the blend texels are not read without filtering")
        check("Look.BlendTexelsLayers(" in code, f"{what}: the texels are not decoded one by one")
    check("shares = float4(1.0 - alpha, 0.5 * alpha, 0.5 * alpha, 0.0)" in gpu["VERTEX_HLSL"],
          "the morph of the horizon does not share 1 - alpha, alpha / 2, alpha / 2")
    clipmap = (scripts.parent / "Source" / "TerrainEngine" / "Private")
    for name, source in (("HorizonBlend", "TerrainHorizonClipmap.cpp"),
                         ("SurfaceBlend", "TerrainHorizonClipmap.cpp"),
                         ("SurfaceBlends", "TerrainHorizonClipmapBlend.cpp")):
        check(f'TEXT("{name}")' in (clipmap / source).read_text(encoding="utf-8"),
              f"{source} sets no {name}")
    # Unreal builds its sources unified: a name of the plugin must not be an enumerator of an
    # unscoped enum of the engine (EMaterialParameterAssociation: C2872, an ambiguous symbol).
    private = scripts.parent / "Source" / "TerrainEngine" / "Private"
    for source in sorted(private.glob("TerrainHorizon*.cpp")):
        text = source.read_text(encoding="utf-8")
        for name in ("LayerParameter", "BlendParameter", "GlobalParameter"):
            check(re.search(rf"\b{name}\b", text) is None,
                  f"{source.name}: the name {name} collides with EMaterialParameterAssociation")
    return declared


# The reference of the HLSL in Python (Scripts/blend_layers_hlsl.py), line for line.
def f32(x):
    """x rounded to a float of 32 bits, as the GPU holds it."""
    return struct.unpack("f", struct.pack("f", x))[0]


PRESENT = 1 << 23  # biome_blend.hpp: kBlendPresentBit
DUNE = 15  # biome_blend.hpp: kBlendDuneShift
WATER = 18  # biome_blend.hpp: kBlendWaterShift


def whole(channel):
    """(uint)max(t, 0.0) of a channel read as a float of 32 bits: the integer it holds, exactly."""
    return int(f32(max(f32(channel), 0.0)))


def texel_blend(channels):
    """TexelBlend of the 4 float channels of a blend texel (Bridge/tile_blend_atlas.hpp, agent
    49): its 4 fields, and whether its present bit is set."""
    v = [whole(c) for c in channels]
    return v, (v[0] & PRESENT) != 0


FLOOR = 16.0 / 127.0  # biome_blend.hpp: kErgFloorShare


def add_blend(v, share, wb, wm, wd=None, counts=(12, 8)):
    if wd is not None:
        wd[0] += share * ((v[1] >> DUNE) & 127) / 127.0
    weights_b = [(v[2] & 255) / 255.0, ((v[2] >> 8) & 255) / 255.0]
    weights_b.append(max(1.0 - weights_b[0] - weights_b[1], 0.0))
    weights_t = [(v[3] & 255) / 255.0, ((v[3] >> 8) & 255) / 255.0]
    weights_t.append(max(1.0 - weights_t[0] - weights_t[1], 0.0))
    for k in range(3):
        biome = min((v[0] >> (6 * k)) & 63, counts[0] - 1)
        material = min((v[1] >> (5 * k)) & 31, counts[1] - 1)
        wb[biome] += share * weights_b[k]
        wm[material] += share * weights_t[k]


def finish_blend(wb, wm, biomes, wd=0.0):
    dune = min(max((min(max(wd, 0.0), 1.0) - FLOOR) / (1.0 - FLOOR), 0.0), 1.0)
    a = [sum(wb[n] * biomes[n][0][i] for n in range(len(biomes))) for i in range(4)]
    b = [sum(wb[n] * biomes[n][1][i] for n in range(len(biomes))) for i in range(4)]
    c = [sum(wb[n] * biomes[n][2][i] for n in range(len(biomes))) for i in range(2)]
    ocean, desert = wb[0], wb[2]
    bed = [ocean * biomes[0][1][i] + (1.0 - ocean) * (0.5, 0.0, 0.0, 0.5)[i] for i in range(4)]
    soil = wm[3] + wm[6]
    keep = wm[2] + wm[5]
    la = [keep * a[i] + 0.3 * soil * a[i] + wm[0] * ocean * biomes[0][0][i] for i in range(4)]
    lb = [keep * b[i] + soil * (0.3 * b[i] + (0.7, 0, 0, 0)[i]) + wm[0] * bed[i]
          + wm[1] * ((1.0 - dune) * (0.0, 1.0 - desert, desert, 0.0)[i] + dune * (0, 1, 0, 0)[i])
          + (wm[4] + wm[7]) * (0, 0, 0, 1)[i]
          for i in range(4)]
    if len(wb) > 15:  # the grass of the deserts is the grass of an oasis (agent 49)
        deserts = (2, 14, 15)
        total = sum(wb[d] for d in deserts)
        la = [la[i] + wm[2] * (total * (0.7, 0.3, 0.0, 0.0)[i]
                               - sum(wb[d] * biomes[d][0][i] for d in deserts)) for i in range(4)]
        lb = [lb[i] - wm[2] * sum(wb[d] * biomes[d][1][i] for d in deserts) for i in range(4)]
    if len(wm) > 10:  # the soils of the deserts (agent 49): gravel, stone, crust
        lb = [lb[i] + wm[8] * (0.0, 0.0, 0.65, 0.35)[i] + wm[9] * (0.25, 0.0, 0.0, 0.75)[i]
              + wm[10] * (0.45, 0.55, 0.0, 0.0)[i] for i in range(4)]
    lc = [c[0] + (1.0 - c[0]) * wm[5], c[1] + (1.0 - c[1]) * wm[4]]
    return la, lb, lc, wb, wm


def blend_texels_layers(texels, shares, biomes, dunes=None, materials=11):
    wb, wm, wd = [0.0] * len(biomes), [0.0] * materials, [0.0]
    for floats, share in zip(texels, shares):
        v, present = texel_blend(floats)
        if share > 0.0:
            if not present:
                return None
            add_blend(v, share, wb, wm, wd, (len(biomes), materials))
    if dunes is not None:
        dunes.append(wd[0])
    return finish_blend(wb, wm, biomes, wd[0])


def channels(biomes, biome_weights, materials, material_weights, dunes=0):
    """The 4 float channels of Bridge/tile_blend_atlas.hpp of a blend (weights in 255ths, the share
    of the dunes in 127ths), as the GPU holds them: integers below 2^24, exact in floats."""
    fields = [PRESENT, dunes << DUNE, biome_weights[0] | biome_weights[1] << 8,
              material_weights[0] | material_weights[1] << 8]
    for k in range(3):
        fields[0] |= biomes[k] << (6 * k)
        fields[1] |= materials[k] << (5 * k)
    return [f32(float(field)) for field in fields]


def check_reference(biomes):
    # Tropical forest (6) and savanna (3) on grass (2); desert (2) and beach (1) on sand (1) and
    # dirt (3): texels of different layers.
    forest = channels((6, 3, 4), (150, 80), (2, 3, 2), (200, 55))
    desert = channels((2, 1, 2), (255, 0), (1, 3, 1), (128, 127))
    v, present = texel_blend(forest)
    check(present and v == [PRESENT | 6 | 3 << 6 | 4 << 12, 2 | 3 << 5 | 2 << 10,
                            150 | 80 << 8, 200 | 55 << 8], "the channels decode wrong")
    check(not texel_blend([0, 0, 0, 0])[1], "a texel never written reads as a blend")
    for alpha in (0.0, 0.5, 1.0):
        # The morph: this sample (forest) and the two of the coarser mean (desert, forest).
        layers = blend_texels_layers((forest, desert, forest),
                                     (1.0 - alpha, 0.5 * alpha, 0.5 * alpha), biomes)
        check(layers is not None, "a present texel refused")
        la, lb, lc, wb, wm = layers
        check(abs(sum(wb) - 1.0) < 1e-6 and abs(sum(wm) - 1.0) < 1e-6,
              f"the weights of the morph at {alpha} do not sum to 1: {sum(wb)}, {sum(wm)}")
        check(abs(sum(la) + sum(lb) - 1.0) < 1e-6,
              f"the ground layers of the morph at {alpha} do not sum to 1: {sum(la) + sum(lb)}")
        check(all(x >= -1e-9 for x in la + lb + lc), "a negative layer")
    # Halfway: half of the desert weighs half of the weights, the ids never interpolated (an
    # interpolated id 4 = (6 + 2) / 2 would be grassland, which neither texel holds as much).
    _, _, _, wb, _ = blend_texels_layers((forest, desert, forest), (0.5, 0.25, 0.25), biomes)
    check(abs(wb[2] - 0.25 * 1.0) < 1e-6 and abs(wb[6] - 0.75 * 150 / 255) < 1e-6,
          "the weights halfway are not the mean of the weights")
    check(abs(wb[4] - 0.75 * 25 / 255) < 1e-6, "an interpolated id took a weight")
    # The bilinear mean of 4 under the tiles, one texel without blend refused unless its share is 0.
    bilinear = blend_texels_layers((forest, desert, desert, forest), (0.1, 0.2, 0.3, 0.4), biomes)
    check(bilinear is not None and abs(sum(bilinear[3]) - 1.0) < 1e-6, "the bilinear mean")
    check(blend_texels_layers((forest, [0, 0, 0, 0]), (0.5, 0.5), biomes) is None,
          "a texel without blend taken")
    check(blend_texels_layers((forest, [0, 0, 0, 0]), (1.0, 0.0), biomes) is not None,
          "a texel without blend and of no share refused")
    check_dunes(biomes)
    check_water()
    # Grass near the water of a rocky desert (2) and of a sand desert (14): the grass of an oasis,
    # drawn far as near (the green band along a river of a desert, agent 49).
    for desert in (2, 14):
        oasis = channels((desert, desert, desert), (255, 0), (2, 2, 2), (255, 0))
        la, lb, _, _, _ = blend_texels_layers((oasis,), (1.0,), biomes)
        check(abs(la[0] - 0.7) < 1e-6 and abs(la[1] - 0.3) < 1e-6 and sum(lb) < 1e-6,
              f"the grass near the water of the desert {desert}: {la}, {lb}")
    # The soils of the deserts (gravel 8, stone 9, crust 10) in a rocky desert (2): the ground
    # layers still sum to 1, through the morph.
    soils = channels((2, 2, 2), (255, 0), (8, 9, 10), (100, 90))
    for alpha in (0.0, 0.5, 1.0):
        la, lb, _, _, wm = blend_texels_layers((soils, forest, soils),
                                               (1.0 - alpha, 0.5 * alpha, 0.5 * alpha), biomes)
        check(abs(sum(la) + sum(lb) - 1.0) < 1e-6 and wm[8] > 0.0,
              f"the soils of the deserts at the morph {alpha}: {la}, {lb}")


def check_water():
    """TexelWater reads every kind of water (4 bits from WATER_SHIFT of the biomes) under the
    present bit and beside the biome ids of the 3 slots, never one of their bits."""
    wrong = 0
    for kind in range(16):
        for ids in (0, (1 << 18) - 1):
            v, _ = texel_blend([float(PRESENT | ids | kind << WATER), 0.0, 0.0, 0.0])
            wrong += (v[0] >> WATER) & 15 != kind or v[0] & ((1 << 18) - 1) != ids
    check(wrong == 0, f"{wrong} kinds of water decode wrong")


def check_dunes(biomes):
    """Every integer of a field (24 bits) read back exactly from its float, the share of the dunes
    of an erg (agent 34) from bit 15 of the materials, and its mean at the morph: half a texel of
    dunes beside a desert floor."""
    wrong = 0
    for value in list(range(0, 1 << 24, 4099)) + [(1 << 24) - 1, PRESENT, PRESENT - 1]:
        wrong += texel_blend([float(PRESENT), float(value), 0.0, 0.0])[0][1] != value
    check(wrong == 0, f"{wrong} values of a field decode wrong")
    # Every biome id in every slot under the present bit (from 2^23 on, a rounding t + 0.5 reads an
    # odd integer one too high).
    wrong = sum(texel_blend([float(PRESENT | b << (6 * k)), 0.0, 0.0, 0.0])[0][0]
                != (PRESENT | b << (6 * k)) for b in range(64) for k in range(3))
    check(wrong == 0, f"{wrong} biome ids decode wrong under the present bit")
    for units in range(128):
        v, _ = texel_blend(channels((2, 2, 2), (255, 0), (1, 1, 1), (255, 0), units))
        wrong += ((v[1] >> DUNE) & 127) != units
    check(wrong == 0, "a share of the dunes decodes wrong")
    # Sand of the desert (2) on dunes (share 127) and on the floor of the erg (16): the morph.
    dunes = channels((2, 2, 2), (255, 0), (1, 1, 1), (255, 0), 127)
    floor = channels((2, 2, 2), (255, 0), (1, 1, 1), (255, 0), 16)
    for alpha in (0.0, 0.25, 0.5, 0.75, 1.0):
        shares = (1.0 - alpha, 0.5 * alpha, 0.5 * alpha)
        seen = []
        la, lb, _, _, _ = blend_texels_layers((dunes, floor, floor), shares, biomes, seen)
        expected = (1.0 - alpha) * 1.0 + alpha * 16.0 / 127.0
        check(abs(seen[0] - expected) < 1e-6, f"the share of the dunes at the morph {alpha}: "
              f"{seen[0]}, not {expected}")
        dune = (expected - FLOOR) / (1.0 - FLOOR)
        check(abs(lb[1] - dune) < 1e-6 and abs(lb[2] - (1.0 - dune)) < 1e-6,
              f"the sand of the dunes at the morph {alpha}: {lb}")
        check(abs(sum(la) + sum(lb) - 1.0) < 1e-6, f"the layers at the morph {alpha}")


def header_constant(path, name):
    """The integer of an inline constexpr of a header of the core (a literal or a sum of them)."""
    found = re.search(rf"constexpr int {name} = ([^;]+);", path.read_text(encoding="utf-8"))
    return eval(found.group(1), {"kBlendSlots": 3, "kMaterialIdBits": 5, "kBiomeIdBits": 6}) \
        if found else None


def check_counts(root, blend):
    """The counts and the fields of blend_layers_hlsl.py are those of the core (agent 49)."""
    generation = root.parent / "terrain_core" / "include" / "terrain" / "generation"
    expected = {"BIOME_COUNT": ("biome.hpp", "kBiomeCount"),
                "MATERIAL_COUNT": ("surface_material.hpp", "kSurfaceMaterialCount"),
                "BIOME_ID_BITS": ("biome_blend.hpp", "kBiomeIdBits"),
                "MATERIAL_ID_BITS": ("biome_blend.hpp", "kMaterialIdBits"),
                "PRESENT_BIT": ("biome_blend.hpp", "kBlendPresentBit"),
                "DUNE_SHIFT": ("biome_blend.hpp", "kBlendDuneShift"),
                "WATER_SHIFT": ("biome_blend.hpp", "kBlendWaterShift"),
                "COVER_SHIFT": ("biome_blend.hpp", "kBlendCoverShift"),
                "COVER_KIND_SHIFT": ("biome_blend.hpp", "kBlendCoverKindShift")}
    for name, (header, constant) in expected.items():
        value = header_constant(generation / header, constant)
        check(value == getattr(blend, name), f"{name} of blend_layers_hlsl.py is "
              f"{getattr(blend, name)}, {constant} of {header} is {value}")
    check(PRESENT == 1 << blend.PRESENT_BIT and DUNE == blend.DUNE_SHIFT
          and WATER == blend.WATER_SHIFT,
          "the reference decodes other fields than the HLSL")
    check(blend.SURFACE_BIOME_STRIDE == 32 and blend.SURFACE_WATER_CODE == 2048,
          "the packed surface of Bridge/terrain_surface.hpp")


def main(root):
    scripts = root / "Scripts"
    sys.path.insert(0, str(scripts))
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    sys.modules["unreal"] = Anything()
    import temporary_terrain_look as look  # noqa: E402  (after the fake module unreal)
    import unreal_loading_scripts_test as loading  # noqa: E402

    # The scan itself: it catches a name declared again in an inner scope, and nothing else.
    check(shadowed("float a = 1.0; { float a = 2.0; }", loading.DECLARATION)[0] == ["a"],
          "the scan misses a shadowed name")
    check(not shadowed("{ float a = 1.0; } { float a = 2.0; }", loading.DECLARATION)[0],
          "the scan takes sibling scopes for one")
    declared = check_code(look.struct_hlsl(), loading, scripts)
    check(declared > 100, f"only {declared} declarations scanned")
    for biome in look.BIOMES:
        check(abs(sum(biome[0]) + sum(biome[1]) - 1.0) < 1e-9, "a biome of layers not summing to 1")
    check_counts(root, look.hlsl.blend_code)
    check_reference(look.BIOMES)
    print(f"horizon blend scripts: {len(failures)} failure(s), {declared} declarations checked")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(pathlib.Path(sys.argv[1])))
