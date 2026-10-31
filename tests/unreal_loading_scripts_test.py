"""The reveal of the levels of the GPU horizon in its material (agent 38), without Unreal:
terrain_unreal/Scripts/horizon_reveal.py and how create_horizon_gpu_material.py and
TerrainHorizonClipmap.cpp use it.

- Every name the HLSL reads is one of its inputs or of Unreal; braces and parentheses balanced.
- The material declares the parameters of the reveal and multiplies its opacity mask by it.
- The C++ of the plugin sets those parameters by the same names.
- The formula, run here: a level kept whole at Reveal 1 without EdgeFade, nothing at Reveal 0, its
  outer band dissolved with EdgeFade 1 and its middle kept.
Usage: python3 unreal_loading_scripts_test.py <terrain_unreal>
"""

import pathlib
import re
import sys
import types

sys.dont_write_bytecode = True
failures = []


def check(passed, what):
    if not passed:
        failures.append(what)
        print(f"FAILED: {what}")


def keep(local, cells, reveal, edge_fade, edge_cells):
    """REVEAL_HLSL before the dither: the share of the pixel kept."""
    half = cells * 0.5
    d = max(abs(local[0] - half), abs(local[1] - half))
    edge = min(max((half - d) / max(edge_cells, 1.0), 0.0), 1.0)
    reveal = min(max(reveal, 0.0), 1.0)
    edge_fade = min(max(edge_fade, 0.0), 1.0)
    return reveal * (1.0 + (edge - 1.0) * edge_fade)


def main(root):
    scripts = root / "Scripts"
    sys.path.insert(0, str(scripts))
    sys.modules["unreal"] = types.SimpleNamespace()
    import horizon_reveal as reveal  # noqa: E402  (after the fake module unreal)

    code = re.sub(r"//[^\n]*", "", reveal.HLSL)
    used = set(re.findall(r"\b[A-Z][A-Za-z0-9]*\b", code))
    known = set(reveal.INPUTS) | {"Parameters", "SvPosition", "View", "StateFrameIndexMod8"}
    check(used <= known, f"HLSL names not declared: {sorted(used - known)}")
    check(code.count("{") == code.count("}") and code.count("(") == code.count(")"),
          "HLSL braces or parentheses unbalanced")
    material = (scripts / "create_horizon_gpu_material.py").read_text(encoding="utf-8")
    check("list(reveal.SCALARS.items())" in material, "the material declares no reveal parameter")
    check("reveal.mask(material, g, nodes, square)" in material,
          "the opacity mask of the material is not multiplied by the reveal")
    clipmap = (root / "Source" / "TerrainEngine" / "Private" /
               "TerrainHorizonClipmap.cpp").read_text(encoding="utf-8")
    for name in reveal.SCALARS:
        check(f'TEXT("{name}")' in clipmap, f"TerrainHorizonClipmap.cpp sets no {name}")
    check(keep((64, 64), 128, 1.0, 0.0, 24) == 1.0, "a level shown whole is not kept whole")
    check(keep((0, 64), 128, 1.0, 0.0, 24) == 1.0, "its border is cut without EdgeFade")
    check(keep((64, 64), 128, 0.0, 0.0, 24) == 0.0, "a hidden level is kept")
    check(keep((1, 64), 128, 1.0, 1.0, 24) < 0.05, "the outer band is not dissolved")
    check(keep((64, 64), 128, 1.0, 1.0, 24) == 1.0, "the middle of the level is dissolved")
    check(abs(keep((64 - 52, 64), 128, 0.5, 0.5, 24) - 0.5 * (1 - 0.5 * (1 - 12 / 24))) < 1e-9,
          "the reveal and the dissolve do not multiply")
    print(f"loading scripts: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(pathlib.Path(sys.argv[1])))
