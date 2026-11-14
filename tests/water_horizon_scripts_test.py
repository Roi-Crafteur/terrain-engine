"""The scripts of the materials changed by agent 40 (commits 13 to 15), without Unreal: one check
per change, each behind its switch (Terrain.Param), 1 by default and 0 for the look before.

- AdaptiveSplit (horizon_vertex_hlsl.py): declared at 1; the contour rule of the HLSL is the one
  of the C++ (Bridge/horizon_split.cpp: mainDiagonal); the center of a cell is the blue vertex of
  the grids (TerrainHorizonGrids.cpp), shifted by one pitch along x, the scale of the grid
  components (TerrainHorizonClipmap.cpp); the water of the horizon takes the sample of the vertex.
- HorizonFootCurv (horizon_ground_hlsl.py): declared at 1; the steepness of the foot and the reach
  and span of the curvature those of the tiles (create_temporary_terrain_material.py); the
  material of the horizon reads them (WF), the tiles theirs as before; every name its code reads
  is an input of its node.
- SeamNormal (create_temporary_terrain_material.py): declared at 1, its band ending before the edge
  of the tiles at 512 m.
- In each new HLSL, no variable named as a word HLSL keeps (half, fixed, sample, linear, point);
  unreal_water_scripts_test.py runs every script and checks the scopes of every Custom node.
Usage: python3 water_horizon_scripts_test.py <repository>
"""

import importlib
import pathlib
import re
import sys
import types

failures = []


def check(passed, what):
    if not passed:
        failures.append(what)
        print(f"FAILED: {what}")


def defaults(record):
    """The default value of each scalar parameter a run of a script declared."""
    values, name = {}, None
    for leaf, args in record["calls"]:
        if leaf != "set_editor_property" or len(args) < 2:
            continue
        if args[0] == "parameter_name":
            name = args[1]
        elif args[0] == "default_value" and name is not None and isinstance(args[1], float):
            values[name] = args[1]
    return values


def kept_words(code):
    """The variables a code declares with a name HLSL keeps."""
    bare = re.sub(r"//[^\n]*", "", code)
    return re.findall(r"\b(?:bool|int[1-4]?|float[1-4]?)\s+(half|fixed|sample|linear|point)\b",
                      bare)


def check_adaptive_split(root, scripts, water, horizon_defaults):
    vertex = importlib.import_module("horizon_vertex_hlsl")
    check(horizon_defaults.get("AdaptiveSplit") == 1.0, "AdaptiveSplit is not declared at 1")
    private = root / "terrain_unreal/Source/TerrainEngine/Private"
    cpp = (private / "Bridge/horizon_split.cpp").read_text()
    check("std::abs(h00 - h11) < std::abs(h10 - h01)" in cpp and
          "return abs(h00 - h11) < abs(h10 - h01);" in vertex.GROUND_HLSL,
          "the contour rule of the HLSL is not that of mainDiagonal")
    grids = (private / "TerrainHorizonGrids.cpp").read_text()
    check("bCenter ? 1.0f : 0.0f, 1.0f)" in grids and "Kind.b > 0.5 && AdaptiveSplit > 0.5" in
          vertex.VERTEX_HLSL, "the center of a cell is not the blue vertex the shader moves")
    clipmap = (private / "TerrainHorizonClipmap.cpp").read_text()
    check("SetWorldScale3D(FVector(PitchCm, PitchCm, 1.0))" in clipmap and
          "shiftCm = -PitchCm;" in vertex.VERTEX_HLSL and
          "return float3(shiftCm, 0.0, z);" in vertex.VERTEX_HLSL,
          "a moved center is not shifted by one pitch along x")
    gpu = (scripts / "create_horizon_gpu_material.py").read_text()
    check("water_nodes.vertex(material, g, nodes, kind, vertex)" in gpu and
          'g.connect(ground, "LocalOut", custom, "Local")' in water and
          "LocalOut = float2(ik);" in vertex.VERTEX_HLSL,
          "the water of the horizon does not take the sample of the vertex")
    code = vertex.vertex_hlsl("(t).a")
    check(not kept_words(code), f"the vertex shader names a variable {kept_words(code)}")


def check_foot_curv(scripts, horizon_defaults):
    ground = importlib.import_module("horizon_ground_hlsl")
    look = importlib.import_module("temporary_terrain_look")
    check(horizon_defaults.get("HorizonFootCurv") == 1.0, "HorizonFootCurv is not declared at 1")
    tiles = (scripts / "create_temporary_terrain_material.py").read_text()
    foot = re.search(r"c\.z = saturate\(\(steep - ([0-9.]+)\) / ([0-9.]+)\);", tiles)
    check(foot is not None and float(foot.group(1)) == ground.FOOT_FROM and
          float(foot.group(2)) == ground.FOOT_RAMP,
          "the foot of the horizon is not that of the tiles")
    code = ground.foot_curv_hlsl()
    check("ij + float2(2.0, 0.0)" in tiles and "float span = 0.02 * SurfacePitchCm;" in tiles and
          "PitchCm < 1000.0 ? 2.0 : 1.0" in code and "reach * PitchCm / 100.0" in code,
          "the curvature of the horizon at level 0 does not reach as the tiles (2 samples, 16 m)")
    check("FOOT_FROM" not in code and "FOOT_RAMP" not in code, "a placeholder is left")
    used = set(re.findall(r"\b[A-Z][A-Za-z0-9]*\b", re.sub(r"//[^\n]*", "", code)))
    known = {"FHorizonFootCurv", "FootCurv", "Height", "At", "Texture2D", "Load", "BRANCH", "T"}
    check(used - known <= set(ground.INPUTS),
          f"the foot node reads undeclared names: {sorted(used - known - set(ground.INPUTS))}")
    far, near = look.shade_hlsl(False), look.shade_hlsl(True)
    check("float curv = WF.y * CurvatureGain;" in far and "WF.x" in far and "WF" not in near and
          "float curv = W3.w * CurvatureGain;" in near,
          "the horizon does not read its foot and curvature (WF), or the tiles changed")
    check(not kept_words(code), f"the foot node names a variable {kept_words(code)}")


def check_seam_normal(scripts, tile_defaults):
    source = (scripts / "create_temporary_terrain_material.py").read_text()
    check(tile_defaults.get("SeamNormal") == 1.0, "SeamNormal is not declared at 1")
    near, far = tile_defaults.get("SeamNearM", 0.0), tile_defaults.get("SeamFarM", 1e9)
    check(0.0 < near < far <= 512.0, f"the band of SeamNormal ({near} to {far} m) is not within "
                                     "the tiles")
    check("k = lerp(k, 1.0, SeamNormal * saturate((Dist - SeamNearM)" in source,
          "the normal of the tiles does not lean fully toward the horizon at their edge")


def main(root):
    root = pathlib.Path(root)
    scripts = root / "terrain_unreal/Scripts"
    sys.path.insert(0, str(scripts))
    sys.path.insert(0, str(pathlib.Path(__file__).parent))
    sys.modules["unreal"] = types.SimpleNamespace(CustomMaterialOutputType=types.SimpleNamespace(
        CMOT_FLOAT1=1, CMOT_FLOAT2=2, CMOT_FLOAT3=3, CMOT_FLOAT4=4))
    runner = importlib.import_module("unreal_water_scripts_test")
    horizon = defaults(runner.run_as_unreal(scripts, "create_horizon_gpu_material.py", False))
    tiles = defaults(runner.run_as_unreal(scripts, "create_temporary_terrain_material.py", False))
    check(not runner.failures, f"the scripts do not run: {runner.failures}")
    sys.modules["unreal"] = types.SimpleNamespace(CustomMaterialOutputType=types.SimpleNamespace(
        CMOT_FLOAT1=1, CMOT_FLOAT2=2, CMOT_FLOAT3=3, CMOT_FLOAT4=4))
    water = (scripts / "horizon_water_nodes.py").read_text()
    check_adaptive_split(root, scripts, water, horizon)
    check_foot_curv(scripts, horizon)
    check_seam_normal(scripts, tiles)
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
