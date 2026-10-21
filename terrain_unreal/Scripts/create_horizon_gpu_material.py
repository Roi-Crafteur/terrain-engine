"""Creates /Game/TerrainEngine/M_TerrainHorizonGpu in the open Unreal project.

The material of the GPU horizon (phase 13, step 2). Its fixed grids hold vertices at (i, k, 0) in
cells (TerrainHorizonGrids.h); in the vertex shader it reads the texel of the sample (i, k) of the
texture of the level (Bridge/horizon_textures.hpp: height, slopes, packed color, at the slot
(first slot + (i, k)) mod 257), morphs toward the next coarser level near the border of the window
(Bridge/horizon_morph.hpp: the same factor and targets, the normals toward the texels of the
coarser level), lowers the skirt, puts the sea square at the sea level, drops everything by
HorizonDropCm and curves the planet (d^2 / 2R, d the distance to the camera, as M_TerrainHorizon).
The normal (world space) and the color go to the pixel shader through vertex interpolators.
ATerrainWorld (UTerrainHorizonClipmap) sets its parameters per level.

The asset belongs to the game project, not to this repository. Run the script once per project,
in the Unreal Editor (the Python Editor Script Plugin must be enabled):
    Tools > Execute Python Script... > terrain_unreal/Scripts/create_horizon_gpu_material.py
"""

import math

import unreal

PACKAGE_PATH = "/Game/TerrainEngine"
ASSET_NAME = "M_TerrainHorizonGpu"
ROUGHNESS = 0.9
DEFAULT_TEXTURE = "/Engine/EngineResources/Black"
SCALARS = {
    "Cells": 256.0, "FirstSlotX": 0.0, "FirstSlotZ": 0.0, "CoarseX": 0.0, "CoarseZ": 0.0,
    "CoarseFirstX": 0.0, "CoarseFirstZ": 0.0, "HasCoarser": 0.0, "MorphCells": 32.0,
    "SkirtCm": 200.0, "DropCm": 100.0, "PlanetRadiusCm": 60_030_976.0 / (2.0 * math.pi) * 100.0,
}

VERTEX_HLSL = r"""
int side = (int)Cells + 1;
int2 first = int2((int)FirstSlotX, (int)FirstSlotZ);
int2 coarseFirst = int2((int)CoarseFirstX, (int)CoarseFirstZ);
int2 inCoarser = int2((int)CoarseX, (int)CoarseZ);
int2 ik = int2(round(Local.xy));
float4 own = Tex.Load(int3((ik + first) % side, 0));
// The samples whose mean the coarser level shows here (Bridge/horizon_morph.hpp).
int2 s0 = ik;
int2 s1 = ik;
bool oddI = (ik.x & 1) != 0;
bool oddK = (ik.y & 1) != 0;
if (oddI && oddK) { s0 = ik + int2(1, -1); s1 = ik + int2(-1, 1); }
else if (oddI) { s0 = ik - int2(1, 0); s1 = ik + int2(1, 0); }
else if (oddK) { s0 = ik - int2(0, 1); s1 = ik + int2(0, 1); }
float4 a = Tex.Load(int3((s0 + first) % side, 0));
float4 b = Tex.Load(int3((s1 + first) % side, 0));
float4 ca = Coarse.Load(int3((inCoarser + s0 / 2 + coarseFirst) % side, 0));
float4 cb = Coarse.Load(int3((inCoarser + s1 / 2 + coarseFirst) % side, 0));
float distance = max(abs(ik.x - Viewer.x), abs(ik.y - Viewer.y));
float alpha = HasCoarser > 0.5 ? saturate((distance - (Cells * 0.5 - 2.0 - MorphCells)) / MorphCells) : 0.0;
// Colors packed as r + 256 g + 65536 b (Bridge/horizon_textures.hpp), exact in floats.
#define HORIZON_UNPACK(v) (float3(fmod(v, 256.0), fmod(floor((v) / 256.0), 256.0), floor((v) / 65536.0)) / 255.0)
float height = lerp(own.r, 0.5 * (a.r + b.r), alpha);
float2 slope = lerp(own.gb, 0.5 * (ca.gb + cb.gb), alpha);
float3 color = lerp(HORIZON_UNPACK(own.a), 0.5 * (HORIZON_UNPACK(a.a) + HORIZON_UNPACK(b.a)), alpha);
#undef HORIZON_UNPACK
float z = height * 100.0 - DropCm;
if (Kind.r > 0.5) { z -= SkirtCm; }
// The sea square under the tiles, at the sea level in kSeaColor (Bridge/horizon_mesh.hpp).
if (Kind.g > 0.5) { z = 0.0; slope = float2(0.0, 0.0); color = float3(28.0, 66.0, 102.0) / 255.0; }
z -= dot(CameraRel, CameraRel) / (2.0 * PlanetRadiusCm);
NormalOut = normalize(float3(-slope.x, -slope.y, 1.0));
ColorOut = color;
return float3(0.0, 0.0, z);
"""


def expression(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def connect(source, source_output, target, target_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, source_output, target, target_input):
        raise RuntimeError(f"cannot connect {source_output} to {target_input}")


def texture_parameter(material, name, y):
    node = expression(material, unreal.MaterialExpressionTextureObjectParameter, -1600, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("texture", unreal.EditorAssetLibrary.load_asset(DEFAULT_TEXTURE))
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return node


def custom_node(material):
    custom = expression(material, unreal.MaterialExpressionCustom, -900, 0)
    custom.set_editor_property("code", VERTEX_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "HorizonVertex")
    names = ["Tex", "Coarse", "Local", "Kind", "Viewer", "CameraRel"] + list(SCALARS)
    inputs = []
    for name in names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    outputs = []
    for name in ("NormalOut", "ColorOut"):
        output = unreal.CustomOutput()
        output.set_editor_property("output_name", name)
        output.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        outputs.append(output)
    custom.set_editor_property("additional_outputs", outputs)
    return custom


def vertex_inputs(material, custom):
    connect(texture_parameter(material, "HorizonTexels", -400), "", custom, "Tex")
    connect(texture_parameter(material, "CoarseTexels", -250), "", custom, "Coarse")
    local = expression(material, unreal.MaterialExpressionPreSkinnedPosition, -1600, -100)
    connect(local, "", custom, "Local")
    connect(expression(material, unreal.MaterialExpressionVertexColor, -1600, 0), "", custom, "Kind")
    camera = expression(material, unreal.MaterialExpressionCameraPositionWS, -1800, 100)
    viewer = expression(material, unreal.MaterialExpressionTransformPosition, -1600, 100)
    viewer.set_editor_property("transform_source_type",
                               unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    viewer.set_editor_property("transform_type",
                               unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(camera, "", viewer, "")
    viewer_xy = expression(material, unreal.MaterialExpressionComponentMask, -1400, 100)
    viewer_xy.set_editor_property("r", True)
    viewer_xy.set_editor_property("g", True)
    connect(viewer, "", viewer_xy, "")
    connect(viewer_xy, "", custom, "Viewer")
    position = expression(material, unreal.MaterialExpressionWorldPosition, -1600, 200)
    position.set_editor_property(
        "world_position_shader_offset",
        unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE_NO_OFFSETS)
    relative = expression(material, unreal.MaterialExpressionComponentMask, -1400, 200)
    relative.set_editor_property("r", True)
    relative.set_editor_property("g", True)
    connect(position, "", relative, "")
    connect(relative, "", custom, "CameraRel")
    for index, (name, value) in enumerate(SCALARS.items()):
        scalar = expression(material, unreal.MaterialExpressionScalarParameter, -1300,
                            300 + 60 * index)
        scalar.set_editor_property("parameter_name", name)
        scalar.set_editor_property("default_value", value)
        connect(scalar, "", custom, name)


def pixel_outputs(material, custom):
    editing = unreal.MaterialEditingLibrary
    normal = expression(material, unreal.MaterialExpressionVertexInterpolator, -500, 0)
    connect(custom, "NormalOut", normal, "")
    color = expression(material, unreal.MaterialExpressionVertexInterpolator, -500, 150)
    connect(custom, "ColorOut", color, "")
    tint = expression(material, unreal.MaterialExpressionVectorParameter, -500, 300)
    tint.set_editor_property("parameter_name", "DebugTint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    # The default output of a vector parameter is its RGB; its alpha is the amount of the tint.
    tinted = expression(material, unreal.MaterialExpressionLinearInterpolate, -200, 150)
    connect(color, "", tinted, "A")
    connect(tint, "", tinted, "B")
    connect(tint, "A", tinted, "Alpha")
    editing.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(custom, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    roughness = expression(material, unreal.MaterialExpressionConstant, -200, 300)
    roughness.set_editor_property("r", ROUGHNESS)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)


def create_horizon_gpu_material():
    asset_path = f"{PACKAGE_PATH}/{ASSET_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log(f"{asset_path} already exists: nothing to do")
        return unreal.EditorAssetLibrary.load_asset(asset_path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(ASSET_NAME, PACKAGE_PATH, unreal.Material,
                                  unreal.MaterialFactoryNew())
    # The normal of the pixel shader is in world space (the grids are flat, the scale is not).
    material.set_editor_property("tangent_space_normal", False)
    custom = custom_node(material)
    vertex_inputs(material, custom)
    pixel_outputs(material, custom)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.log(f"{asset_path} created")
    return material


create_horizon_gpu_material()
