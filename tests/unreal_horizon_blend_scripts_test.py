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
    members = re.findall(r"\n    (?:bool|void|float[1-4]?|int) ([A-Z][A-Za-z0-9]*)\(", struct)
    check(len(members) == len(set(members)), f"a member of the struct twice: {members}")
    for name in ("AddBlend", "FinishBlend", "BlendLayers", "TexelBlend", "BlendTexelsLayers"):
        check(members.count(name) == 1, f"the struct has no {name}")
    declared = 0
    bodies = re.split(r"\n    (?=(?:bool|void|float[1-4]?|int) [A-Z][A-Za-z0-9]*\()", struct)
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
    return declared


# The reference of the HLSL in Python (Scripts/blend_layers_hlsl.py), line for line.
def texel_blend(channels):
    """TexelBlend of 4 channels of 16 bits read as unsigned normalized floats."""
    v = [int(min(max(c / 65535.0, 0.0), 1.0) * 65535.0 + 0.5) for c in channels]
    return v[0] | ((v[1] & 255) << 16), v[2], v[3], (v[1] & 32768) != 0


def add_blend(ids, lo, hi, share, wb, wm):
    weights_b = [(lo & 255) / 255.0, (lo >> 8) / 255.0]
    weights_b.append(max(1.0 - weights_b[0] - weights_b[1], 0.0))
    weights_t = [(hi & 255) / 255.0, (hi >> 8) / 255.0]
    weights_t.append(max(1.0 - weights_t[0] - weights_t[1], 0.0))
    for k in range(3):
        biome = min((ids >> (4 * k)) & 15, 11)
        material = min((ids >> (12 + 4 * k)) & 15, 7)
        wb[biome] += share * weights_b[k]
        wm[material] += share * weights_t[k]


def finish_blend(wb, wm, biomes):
    a = [sum(wb[n] * biomes[n][0][i] for n in range(12)) for i in range(4)]
    b = [sum(wb[n] * biomes[n][1][i] for n in range(12)) for i in range(4)]
    c = [sum(wb[n] * biomes[n][2][i] for n in range(12)) for i in range(2)]
    ocean, desert = wb[0], wb[2]
    bed = [ocean * biomes[0][1][i] + (1.0 - ocean) * (0.5, 0.0, 0.0, 0.5)[i] for i in range(4)]
    soil = wm[3] + wm[6]
    keep = wm[2] + wm[5]
    la = [keep * a[i] + 0.3 * soil * a[i] + wm[0] * ocean * biomes[0][0][i] for i in range(4)]
    lb = [keep * b[i] + soil * (0.3 * b[i] + (0.7, 0, 0, 0)[i]) + wm[0] * bed[i]
          + wm[1] * (0.0, 1.0 - desert, desert, 0.0)[i] + (wm[4] + wm[7]) * (0, 0, 0, 1)[i]
          for i in range(4)]
    lc = [c[0] + (1.0 - c[0]) * wm[5], c[1] + (1.0 - c[1]) * wm[4]]
    return la, lb, lc, wb, wm


def blend_texels_layers(texels, shares, biomes):
    wb, wm = [0.0] * 12, [0.0] * 8
    for channels, share in zip(texels, shares):
        ids, lo, hi, present = texel_blend(channels)
        if share > 0.0:
            if not present:
                return None
            add_blend(ids, lo, hi, share, wb, wm)
    return finish_blend(wb, wm, biomes)


def channels(biomes, biome_weights, materials, material_weights):
    """The channels of Bridge/horizon_blend_texels.hpp of a blend (weights in 255ths)."""
    ids = 0
    for k in range(3):
        ids |= biomes[k] << (4 * k) | materials[k] << (12 + 4 * k)
    weights = (biome_weights[0] | biome_weights[1] << 8 | material_weights[0] << 16
               | material_weights[1] << 24)
    return [ids & 0xFFFF, (ids >> 16) | 0x8000, weights & 0xFFFF, weights >> 16]


def check_reference(biomes):
    # Tropical forest (6) and savanna (3) on grass (2); desert (2) and beach (1) on sand (1) and
    # dirt (3): texels of different layers.
    forest = channels((6, 3, 4), (150, 80), (2, 3, 2), (200, 55))
    desert = channels((2, 1, 2), (255, 0), (1, 3, 1), (128, 127))
    ids, lo, hi, present = texel_blend(forest)
    check(present and ids == (6 | 3 << 4 | 4 << 8 | 2 << 12 | 3 << 16 | 2 << 20)
          and lo == 150 | 80 << 8 and hi == 200 | 55 << 8, "the channels decode wrong")
    check(not texel_blend([0, 0, 0, 0])[3], "a texel never written reads as a blend")
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
    check_reference(look.BIOMES)
    print(f"horizon blend scripts: {len(failures)} failure(s), {declared} declarations checked")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(pathlib.Path(sys.argv[1])))
