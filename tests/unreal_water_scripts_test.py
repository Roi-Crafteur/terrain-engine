"""The scripts of the materials of the water (agent 27), without Unreal: they are only ever run in
the Unreal Editor, so this checks what can be checked here.

- Every script of terrain_unreal/Scripts compiles.
- The HLSL of the water (water_look_hlsl.py, horizon_water_hlsl.py): no placeholder left, braces
  and parentheses balanced, every name of an input a node reads declared among its inputs, every
  output it writes declared among its outputs; the noise of the ripples, run here, repeats every
  4,096 m.
- At the edge of the tiles, the water of the tiles and the water of the horizon keep complementary
  pixels with the same share and threshold; the plugin copies the whole look of the water to the
  horizon (its lists of names are the parameters of water_look_hlsl.py).
- The scripts that rebuild a material in place reset every setting first
  (material_settings.reset_material), and set none of them by hand.
- Each script that creates a material runs to its end as Unreal runs it (Tools > Execute Python
  Script, `py <file>`): executed in globals of its own, not as the module __main__, against a
  fake module unreal that records the calls, for a new asset and for one rebuilt in place; the
  parameters it must declare are there.
Usage: python3 unreal_water_scripts_test.py <terrain_unreal/Scripts>
"""

import ast
import importlib
import math
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


def constant(source, name):
    """The value of a module constant NAME = [...] of a script, without running it."""
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and any(
                isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise KeyError(name)


# Names a node uses that are not its inputs: HLSL, Unreal, the helpers and the locals of the code.
KNOWN = {"BRANCH", "Parameters", "Water", "FTerrainWater", "PERIOD", "K", "Kind",
         "Fresnel", "Glint", "GetTranslatedWorldPosition", "GetPeriodicWorldOrigin",
         "MaterialExpressionSkyAtmosphereViewLuminance", "Load", "Body", "Waves", "Ripples",
         "Noise", "Hash", "PeriodicHash", "Octave", "HorizonShare", "Dither", "SvPosition",
         "INLAND",
         "SURFACE", "DEPTH", "D", "H", "L", "V", "Vis", "Fs", "NoV", "NoL", "NoH", "VoH"}


def names(code):
    code = re.sub(r"//[^\n]*", "", code)
    return set(re.findall(r"\b[A-Z][A-Za-z0-9]*\b", code))


def check_node(label, code, inputs, outputs):
    check("OCTAVE_LOOP" not in code and "SKY_CODE" not in code and "BODY_CODE" not in code,
          f"{label}: a placeholder is left")
    bare = re.sub(r"//[^\n]*", "", code)  # comments may hold "[0, 1)"
    for opening, closing in ("{}", "()"):
        check(bare.count(opening) == bare.count(closing), f"{label}: {opening}{closing} unbalanced")
    used = names(code) - KNOWN
    written = {name for name in used if name.endswith("Out")}
    check(written <= set(outputs),
          f"{label}: outputs not declared: {sorted(written - set(outputs))}")
    missing = used - written - set(inputs)
    check(not missing, f"{label}: inputs not declared: {sorted(missing)}")


def frac(x):
    return x - math.floor(x)


def octave(p, a, b, k, drift):
    """FTerrainWater::Octave (water_look_hlsl.py), in Python: the gradient of the noise at p."""
    def hash_of(c):
        h = [frac(v * 0.1031) for v in (c[0], c[1], c[0])]
        d = h[0] * (h[1] + 33.33) + h[1] * (h[2] + 33.33) + h[2] * (h[0] + 33.33)
        h = [v + d for v in h]
        return frac((h[0] + h[1]) * h[2])

    def periodic(c, g):
        n = g[0] * g[0] + g[1] * g[1]
        q = (round((c[0] * g[0] + c[1] * g[1]) / n), round((c[1] * g[0] - c[0] * g[1]) / n))
        return hash_of((c[0] - (g[0] * q[0] - g[1] * q[1]), c[1] - (g[0] * q[1] + g[1] * q[0])))

    u = ((a * p[0] + b * p[1]) * k / 4096.0 + drift, (a * p[1] - b * p[0]) * k / 4096.0)
    i = (math.floor(u[0]), math.floor(u[1]))
    f = (u[0] - i[0], u[1] - i[1])
    g = (k * a, -k * b)
    h00, h10 = periodic(i, g), periodic((i[0] + 1, i[1]), g)
    h01, h11 = periodic((i[0], i[1] + 1), g), periodic((i[0] + 1, i[1] + 1), g)
    s = [v * v * (3.0 - 2.0 * v) for v in f]
    corner = h00 - h10 - h01 + h11
    return ((h10 - h00 + corner * s[1]) * 6.0 * f[0] * (1.0 - f[0]),
            (h01 - h00 + corner * s[0]) * 6.0 * f[1] * (1.0 - f[1]))


def check_ripples(look):
    """The octaves of the ripples repeat every 4,096 m in X and Y (nothing moves after a turn of
    the world, nor when the origin of Unreal moves by whole periods), and are not flat."""
    worst = 0.0
    spread = 0.0
    for a, b, k, _, _ in look.OCTAVES:
        for n in range(200):
            p = (37.3 * n % 4096.0, 91.7 * n % 4096.0)
            here = octave(p, a, b, k, 0.37 * n)
            spread = max(spread, abs(here[0]) + abs(here[1]))
            for shift in ((4096.0, 0.0), (0.0, 4096.0), (-8192.0, 4096.0)):
                there = octave((p[0] + shift[0], p[1] + shift[1]), a, b, k, 0.37 * n)
                worst = max(worst, abs(here[0] - there[0]), abs(here[1] - there[1]))
    check(worst < 1e-4 and spread > 0.5,
          f"ripples: {worst:g} apart after 4096 m, spread {spread:g}")
    return worst


class FakeUnreal:
    """The module unreal of the editor, as far as the scripts call it: any attribute is another
    fake, any call is recorded and gives a fake back, except the few answers the scripts read."""

    def __init__(self, record, path="unreal"):
        self._record = record
        self._path = path

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return FakeUnreal(self._record, f"{self._path}.{name}")

    def __call__(self, *args, **kwargs):
        leaf = self._path.rsplit(".", 1)[-1]
        self._record["calls"].append((leaf, args))
        if leaf == "get_num_material_expressions":
            return 0
        if leaf == "does_asset_exist":
            return self._record["exists"]
        if leaf == "set_editor_property" and args and args[0] == "parameter_name":
            self._record["parameters"].add(args[1])
        return FakeUnreal(self._record, f"{self._path}()")


# The parameters a script must declare (the C++ of the plugin and the instances set them).
NEEDED = {
    "create_horizon_gpu_material.py": {"HorizonTexels", "CoarseTexels", "HorizonWater",
                                       "CoarseWater", "InlandWater", "DropCoveredCm",
                                       "DropFadeCm", "LakeWaterColor", "FlowSpeed",
                                       "SkyReflection", "ShoreFadeM"},
    # The band of the tiles over the horizon, placed by TerrainHorizonClipmap.cpp as on the ground.
    "create_water_materials.py": {"LakeWaterColor", "RiverAbsorption", "FlowSpeed",
                                  "CurtainColor", "FoamColor", "FoamDepthM", "SkyReflection",
                                  "ShoreFadeM",
                                  "SurfaceOrigin", "SurfaceCells", "SurfacePitchCm",
                                  "SurfaceCoveredCm", "SurfaceBandCm", "HasSurface"},
    "create_temporary_terrain_material.py": {"SurfaceTexels", "TileBlendTexels"},
}


def run_as_unreal(folder, script, exists):
    """Runs a script as the editor does, its own globals; the calls it made to unreal."""
    record = {"calls": [], "parameters": set(), "exists": exists}
    sys.modules["unreal"] = FakeUnreal(record)
    path = folder / script
    scope = {"__name__": "__main__", "__file__": str(path), "__builtins__": __builtins__}
    try:
        exec(compile(path.read_text(encoding="utf-8"), str(path), "exec"), scope)
    except Exception as error:  # noqa: BLE001  (any failure of the script is the finding)
        check(False, f"{script} ({'rebuilt' if exists else 'new'}): {type(error).__name__}: "
                     f"{error}")
    return record


# Settings UE 5.8 no longer has: set_editor_property fails on them (a warning on every material).
GONE_IN_UE58 = {"cast_shadow_as_masked", "num_customized_uvs"}


def check_junction(look, horizon_code, plugin):
    """At the edge of the tiles one water per pixel: both nodes take the share of the horizon and
    the same threshold, the tiles keep the pixels at or above it, the horizon those under it, and
    no water is faded by the share any more. The plugin copies the whole look of the water of the
    tiles to the horizon: its lists of names are the parameters of water_look_hlsl.py."""
    tile = look.tile_water_hlsl()
    for label, code in (("TerrainWater", tile), ("HorizonInlandWater", horizon_code)):
        shared = "Water.HorizonShare(" in code
        shared = shared and "Water.Dither(Parameters.SvPosition.xy, Time)" in code
        check(shared, f"{label}: not the shared share and threshold of the edge of the tiles")
    check(">= horizon ? 1.0 : 0.0" in tile and ">= share ? 0.0 : 1.0" in horizon_code,
          "the tiles and the horizon do not keep complementary pixels")
    check("1.0 - horizon" not in tile, "the water of the tiles still faded by the share")
    source = (plugin / "TerrainWorldWater.cpp").read_text(encoding="utf-8")
    copied = {}
    for kind in ("Vectors", "Scalars"):
        block = re.search(r"WaterLook" + kind + r"\[\] = \{(.*?)\};", source, re.S)
        copied[kind] = set(re.findall(r'TEXT\("(\w+)"\)', block.group(1))) if block else set()
    check(copied["Vectors"] == set(look.WATER_COLORS) and
          copied["Scalars"] == set(look.WATER_SCALARS),
          f"the look copied to the horizon is not the look of the water: {copied}")


def check_runs(folder):
    for script, needed in NEEDED.items():
        for exists in (False, True):
            record = run_as_unreal(folder, script, exists)
            missing = needed - record["parameters"]
            check(not missing, f"{script}: parameters missing: {sorted(missing)}")
            saved = sum(1 for leaf, _ in record["calls"] if leaf == "save_asset")
            check(saved >= 1, f"{script}: nothing saved")
            reset = {args[0] for leaf, args in record["calls"]
                     if leaf == "set_editor_property" and args and isinstance(args[0], str)}
            check(not reset & GONE_IN_UE58,
                  f"{script}: settings UE 5.8 lacks: {sorted(reset & GONE_IN_UE58)}")


def main(folder):
    folder = pathlib.Path(folder)
    for path in sorted(folder.glob("*.py")):
        try:
            compile(path.read_text(encoding="utf-8"), str(path), "exec")
        except SyntaxError as error:
            check(False, f"{path.name}: {error}")
    sys.path.insert(0, str(folder))
    sys.modules["unreal"] = types.SimpleNamespace(
        CustomMaterialOutputType=types.SimpleNamespace(CMOT_FLOAT1=1, CMOT_FLOAT2=2, CMOT_FLOAT3=3))
    look = importlib.import_module("water_look_hlsl")
    horizon = importlib.import_module("horizon_water_hlsl")
    nodes = importlib.import_module("horizon_water_nodes")
    materials = (folder / "create_water_materials.py").read_text(encoding="utf-8")
    struct = look.struct_hlsl()
    check_node("TerrainWater", struct + look.tile_water_hlsl(),
               constant(materials, "WATER_INPUTS") + list(constant(materials, "SURFACE_SCALARS"))
               + look.look_inputs(),
               ["EmissiveOut", "OpacityOut", "NormalOut", "RoughnessOut"])
    check_node("TerrainFalls", struct + look.FALLS_HLSL, constant(materials, "FALLS_INPUTS"),
               ["OpacityOut"])
    check_node("HorizonWaterVertex", horizon.WATER_VERTEX_HLSL,
               nodes.VERTEX_INPUTS + nodes.VERTEX_SCALARS, nodes.VERTEX_OUTPUTS)
    check_node("HorizonInlandWater", struct + horizon.inland_hlsl(look.SKY_HLSL, look.BODY_CALL),
               nodes.PIXEL_INPUTS + look.look_inputs(), nodes.PIXEL_OUTPUTS)
    for script in ("create_horizon_gpu_material.py", "create_temporary_terrain_material.py",
                   "create_water_materials.py"):
        source = (folder / script).read_text(encoding="utf-8")
        check("reset_material(" in source, f"{script}: the settings of the material not reset")
        check(not re.search(r'material\.set_editor_property\("(blend_mode|shading_model|'
                            r'use_material_attributes|tangent_space_normal|two_sided)"', source),
              f"{script}: a setting of the material set by hand")
    check_runs(folder)
    check_junction(look, horizon.inland_hlsl(look.SKY_HLSL, look.BODY_CALL),
                   folder.parent / "Source" / "TerrainEngine" / "Private")
    worst = check_ripples(look)
    print(f"water scripts: {len(failures)} failure(s), {len(look.OCTAVES)} octaves of ripples "
          f"({worst:.1e} apart after 4,096 m), {len(look.look_inputs())} look parameters")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
