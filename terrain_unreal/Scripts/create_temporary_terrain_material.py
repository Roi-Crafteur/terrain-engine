"""Creates (or rebuilds) /Game/TerrainEngine/M_TemporaryTerrain in the open Unreal project.

The TEMPORARY terrain material of the tiles (phase 14): a readable stand-in until the real
materials, to be replaced then. Its rules and layers are those of temporary_terrain_look.py, shared
with the far horizon (create_horizon_gpu_material.py).

Vertex shader: the 4 texels of level 0 of the GPU horizon around the vertex (texture
SurfaceTexels, its window placed by SurfaceOrigin, SurfaceFirstX/Z, SurfacePitchCm, SurfaceCells:
ATerrainWorld binds them, UTerrainHorizonClipmap::BindSurfaceLook) give the layers of the ground,
bilinear: the same samples as the horizon, so the near and far ground match. Without the GPU
horizon (HasSurface 0), the layers follow the altitude only.

Pixel shader: rock on steep slopes, snow on the gentle cold ground, the ground layers, world
aligned textures near the camera fading to their mean colors far away (temporary_terrain_look.py).

The asset belongs to the game project, not to this repository. Run the script once per project,
and again after updating the plugin, in the Unreal Editor (the Python Editor Script Plugin must be
enabled); it rebuilds an existing asset in place:
    Tools > Execute Python Script... > terrain_unreal/Scripts/create_temporary_terrain_material.py
Its textures come from the Stylescape HD pack (Fab) under /Game/StylescapeHD if the project has
it; otherwise every layer is a flat color (README, section 13).
"""

import importlib
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import temporary_terrain_look as look  # noqa: E402  (after the path of this script)
look = importlib.reload(look)  # the module of this checkout, even if run before

PACKAGE_PATH = "/Game/TerrainEngine"
ASSET_NAME = "M_TemporaryTerrain"
SURFACE_SCALARS = {"SurfaceFirstX": 0.0, "SurfaceFirstZ": 0.0, "SurfaceCells": 256.0,
                   "SurfacePitchCm": 800.0, "HasSurface": 0.0, "WaveOriginX": 0.0,
                   "WaveOriginY": 0.0, "SmoothNormal": 0.8, "SmoothNearM": 60.0,
                   "SmoothFarM": 220.0}
FLOAT1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
FLOAT2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
FLOAT3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
FLOAT4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4

VERTEX_HLSL = r"""
float4 a = float4(1.0, 0.0, 0.0, 0.0);
float4 b = float4(0.0, 0.0, 0.0, 0.0);
float4 c = float4(0.0, 0.0, 0.0, 0.0);
float2 slope = float2(0.0, 0.0);
float altitude = Rel.z / 100.0;
BRANCH if (HasSurface > 0.5)
{
    int side = (int)SurfaceCells + 1;
    int2 first = int2((int)SurfaceFirstX, (int)SurfaceFirstZ);
    float2 ij = Rel.xy / SurfacePitchCm;
    float2 base = clamp(floor(ij), 0.0, SurfaceCells - 1.0);
    float2 f = saturate(ij - base);
    int2 i0 = int2(base);
    float4 t00 = Tex.Load(int3((i0 + first) % side, 0));
    float4 t10 = Tex.Load(int3((i0 + int2(1, 0) + first) % side, 0));
    float4 t01 = Tex.Load(int3((i0 + int2(0, 1) + first) % side, 0));
    float4 t11 = Tex.Load(int3((i0 + int2(1, 1) + first) % side, 0));
    float4 a00, a10, a01, a11, b00, b10, b01, b11;
    float2 c00, c10, c01, c11;
    Look.Layers(t00.a, a00, b00, c00);
    Look.Layers(t10.a, a10, b10, c10);
    Look.Layers(t01.a, a01, b01, c01);
    Look.Layers(t11.a, a11, b11, c11);
    float4 w = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    a = w.x * a00 + w.y * a10 + w.z * a01 + w.w * a11;
    b = w.x * b00 + w.y * b10 + w.z * b01 + w.w * b11;
    c.xy = w.x * c00 + w.y * c10 + w.z * c01 + w.w * c11;
    slope = w.x * t00.gb + w.y * t10.gb + w.z * t01.gb + w.w * t11.gb;
    // The foot of steep slopes: the steepest of the 4 samples (rise over run), from 35 degrees.
    float steep = max(max(length(t00.gb), length(t10.gb)), max(length(t01.gb), length(t11.gb)));
    c.z = saturate((steep - 0.7) / 0.5);
}
else
{
    // Without the texels of the horizon: sand at the sea, grass, alpine grass, cold up high.
    if (altitude < 2.0) { a = float4(0.0, 0.0, 0.0, 0.0); b = float4(0.0, 1.0, 0.0, 0.0); }
    else if (altitude > 1800.0) { a = float4(0.0, 0.0, 0.0, 1.0); }
    c.x = saturate((altitude - 3200.0) / 400.0);
}
c.w = altitude / 1000.0;
W1 = b;
W2 = c;
W3 = float4(slope, HasSurface, 0.0);
return a;
"""

# The normal of the pixel: beyond the tiles of LOD 0 the tiles are coarse Marching Cubes meshes
# whose vertex normals draw saw-teeth and facets along the terminator under a low sun; there the
# normal leans toward the smooth normal of the samples of level 0 (bilinear slopes, the normals of
# the horizon), except on cliffs, overhangs and caves, where the two disagree.
NORMAL_HLSL = r"""
float3 n = normalize(Nv);
if (W3.z < 0.5) { return n; }
float3 smooth = normalize(float3(-W3.xy, 1.0));
float k = SmoothNormal * saturate((Dist - SmoothNearM) / max(SmoothFarM - SmoothNearM, 1.0));
k *= saturate((dot(n, smooth) - 0.6) / 0.2);
return normalize(lerp(n, smooth, k));
"""


def expression(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def connect(source, source_output, target, target_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, source_output, target, target_input):
        raise RuntimeError(f"cannot connect {source_output} to {target_input}")


def custom_node(material, x, y, description, code, inputs, outputs, output_type):
    """A Custom node: its inputs by name, its additional outputs as (name, output type)."""
    custom = expression(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("code", code)
    custom.set_editor_property("output_type", output_type)
    custom.set_editor_property("description", description)
    custom_inputs = []
    for name in inputs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)
    custom_outputs = []
    for name, kind in outputs:
        output = unreal.CustomOutput()
        output.set_editor_property("output_name", name)
        output.set_editor_property("output_type", kind)
        custom_outputs.append(output)
    custom.set_editor_property("additional_outputs", custom_outputs)
    return custom


def scalar(material, name, value, x, y):
    node = expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def vertex_shader(material, scalars):
    """The layers of the vertex, from the texels of level 0 of the horizon."""
    custom = custom_node(
        material, -1400, 0, "TemporaryTerrainVertex", look.struct_hlsl() + VERTEX_HLSL,
        ["Tex", "Rel", "SurfaceFirstX", "SurfaceFirstZ", "SurfaceCells", "SurfacePitchCm",
         "HasSurface"], [("W1", FLOAT4), ("W2", FLOAT4), ("W3", FLOAT4)], FLOAT4)
    texels = expression(material, unreal.MaterialExpressionTextureObjectParameter, -2000, -200)
    texels.set_editor_property("parameter_name", "SurfaceTexels")
    texels.set_editor_property("texture", unreal.EditorAssetLibrary.load_asset(
        "/Engine/EngineResources/Black"))
    texels.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    connect(texels, "", custom, "Tex")
    origin = expression(material, unreal.MaterialExpressionDoubleVectorParameter, -2200, 0)
    origin.set_editor_property("parameter_name", "SurfaceOrigin")
    position = expression(material, unreal.MaterialExpressionWorldPosition, -2200, 150)
    relative = expression(material, unreal.MaterialExpressionSubtract, -1900, 50)
    connect(position, "", relative, "A")
    connect(origin, "", relative, "B")
    to_float = expression(material, unreal.MaterialExpressionComponentMask, -1700, 50)
    for channel in "rgba":
        to_float.set_editor_property(channel, channel != "a")
    connect(relative, "", to_float, "")
    connect(to_float, "", custom, "Rel")
    for name in ["SurfaceFirstX", "SurfaceFirstZ", "SurfaceCells", "SurfacePitchCm", "HasSurface"]:
        connect(scalars[name], "", custom, name)
    interpolated = {}
    for index, output in enumerate(["", "W1", "W2", "W3"]):
        node = expression(material, unreal.MaterialExpressionVertexInterpolator, -1000, 150 * index)
        connect(custom, output, node, "")
        interpolated[f"W{index}"] = node
    return interpolated


def pixel_shader(material, scalars, weights):
    nodes = look.look_parameters(material, True, -2600, 800)
    camera_relative = expression(material, unreal.MaterialExpressionWorldPosition, -1400, 500)
    camera_relative.set_editor_property(
        "world_position_shader_offset",
        unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    position = custom_node(material, -1100, 500, "TemporaryTerrainPosition", look.POSITION_HLSL,
                           ["CameraRel", "WaveOriginX", "WaveOriginY"], [("Dist", FLOAT1)], FLOAT2)
    connect(camera_relative, "", position, "CameraRel")
    connect(scalars["WaveOriginX"], "", position, "WaveOriginX")
    connect(scalars["WaveOriginY"], "", position, "WaveOriginY")
    vertex_normal = expression(material, unreal.MaterialExpressionVertexNormalWS, -1100, 650)
    normal = custom_node(material, -800, 650, "TemporaryTerrainNormal", NORMAL_HLSL,
                         ["Nv", "W3", "Dist", "SmoothNormal", "SmoothNearM", "SmoothFarM"], [],
                         FLOAT3)
    connect(vertex_normal, "", normal, "Nv")
    connect(weights["W3"], "", normal, "W3")
    connect(position, "Dist", normal, "Dist")
    for name in ("SmoothNormal", "SmoothNearM", "SmoothFarM"):
        connect(scalars[name], "", normal, name)
    altitude = expression(material, unreal.MaterialExpressionComponentMask, -800, 400)
    for channel in "rgba":
        altitude.set_editor_property(channel, channel == "a")
    connect(weights["W2"], "", altitude, "")
    meters = expression(material, unreal.MaterialExpressionMultiply, -650, 400)
    meters.set_editor_property("const_b", 1000.0)
    connect(altitude, "", meters, "A")
    inputs = look.shade_inputs() + look.layer_inputs(True)
    shade = custom_node(material, -300, 300, "TemporaryTerrainShade", look.shade_hlsl(True),
                        inputs, [("NormalOut", FLOAT3), ("RoughOut", FLOAT1)], FLOAT3)
    for name in ("W0", "W1", "W2"):
        connect(weights[name], "", shade, name)
    connect(normal, "", shade, "N")
    connect(position, "", shade, "P")
    connect(position, "Dist", shade, "Dist")
    connect(meters, "", shade, "Z")
    for name in inputs:
        if name in nodes:
            connect(nodes[name], "", shade, name)
    editing = unreal.MaterialEditingLibrary
    editing.connect_material_property(shade, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(shade, "NormalOut", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(shade, "RoughOut", unreal.MaterialProperty.MP_ROUGHNESS)


def create_temporary_terrain_material():
    asset_path = f"{PACKAGE_PATH}/{ASSET_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        material = unreal.EditorAssetLibrary.load_asset(asset_path)
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        unreal.log(f"{asset_path} exists: rebuilt in place")
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        material = tools.create_asset(ASSET_NAME, PACKAGE_PATH, unreal.Material,
                                      unreal.MaterialFactoryNew())
    # The normal of the pixel shader is in world space.
    material.set_editor_property("tangent_space_normal", False)
    scalars = {name: scalar(material, name, value, -2200, -900 + 60 * index)
               for index, (name, value) in enumerate(SURFACE_SCALARS.items())}
    weights = vertex_shader(material, scalars)
    pixel_shader(material, scalars, weights)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.log(f"{asset_path} created (temporary terrain material)")
    return material


create_temporary_terrain_material()
