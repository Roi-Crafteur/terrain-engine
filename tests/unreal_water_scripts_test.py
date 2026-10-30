"""The scripts of the materials of the water (agent 27), without Unreal: they are only ever run in
the Unreal Editor, so this checks what can be checked here.

- Every script of terrain_unreal/Scripts compiles.
- The HLSL of the water (water_look_hlsl.py, horizon_water_hlsl.py): no placeholder left, braces
  and parentheses balanced, every name of an input a node reads declared among its inputs, every
  output it writes declared among its outputs.
- The scripts that rebuild a material in place reset every setting first
  (material_settings.reset_material), and set none of them by hand.
Usage: python3 unreal_water_scripts_test.py <terrain_unreal/Scripts>
"""

import ast
import importlib
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
KNOWN = {"BRANCH", "Parameters", "Water", "FTerrainWater", "PERIOD", "PI2", "K", "Kind",
         "Fresnel", "Glint", "GetTranslatedWorldPosition", "GetPeriodicWorldOrigin",
         "MaterialExpressionSkyAtmosphereViewLuminance", "Load", "Body", "Waves", "Ripples",
         "Noise", "Hash", "INLAND", "SURFACE", "DEPTH", "D", "H", "L", "V", "Vis", "Fs", "NoV",
         "NoL", "NoH", "VoH"}


def names(code):
    code = re.sub(r"//[^\n]*", "", code)
    return set(re.findall(r"\b[A-Z][A-Za-z0-9]*\b", code))


def check_node(label, code, inputs, outputs):
    check("RIPPLE_LOOP" not in code and "SKY_CODE" not in code and "BODY_CODE" not in code,
          f"{label}: a placeholder is left")
    bare = re.sub(r"//[^\n]*", "", code)  # comments may hold "[0, 1)"
    for opening, closing in ("{}", "()"):
        check(bare.count(opening) == bare.count(closing), f"{label}: {opening}{closing} unbalanced")
    used = names(code) - KNOWN
    written = {name for name in used if name.endswith("Out")}
    check(written <= set(outputs), f"{label}: outputs not declared: {sorted(written - set(outputs))}")
    missing = used - written - set(inputs)
    check(not missing, f"{label}: inputs not declared: {sorted(missing)}")


def main(folder):
    folder = pathlib.Path(folder)
    for path in sorted(folder.glob("*.py")):
        try:
            compile(path.read_text(encoding="utf-8"), str(path), "exec")
        except SyntaxError as error:
            check(False, f"{path.name}: {error}")
    sys.path.insert(0, str(folder))
    sys.modules["unreal"] = types.SimpleNamespace(
        CustomMaterialOutputType=types.SimpleNamespace(CMOT_FLOAT1=1, CMOT_FLOAT3=3))
    look = importlib.import_module("water_look_hlsl")
    horizon = importlib.import_module("horizon_water_hlsl")
    nodes = importlib.import_module("horizon_water_nodes")
    materials = (folder / "create_water_materials.py").read_text(encoding="utf-8")
    struct = look.struct_hlsl()
    check_node("TerrainWater", struct + look.tile_water_hlsl(),
               constant(materials, "WATER_INPUTS") + look.look_inputs(),
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
    print(f"water scripts: {len(failures)} failure(s), "
          f"{len(look.RIPPLES)} ripples, {len(look.look_inputs())} look parameters")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
