"""Creates (or rebuilds) /Game/TerrainEngine/M_TerrainHorizonGpu in the open Unreal project.

The material of the GPU horizon (phase 13). Its fixed grids hold vertices at (i, k, 0) in cells
(TerrainHorizonGrids.h).

Vertex shader: it reads the texel of the sample (i, k) of the texture of the level
(Bridge/horizon_textures.hpp: height, slopes, packed color or, on the ocean, the ground under the
sea, at the slot (first slot + (i, k)) mod 257), morphs toward the next coarser level near the
border of the window (Bridge/horizon_morph.hpp: the same factor and targets, the normals toward
the texels of the coarser level), lowers the skirt, puts the sea square at the sea level, drops
everything by HorizonDropCm and curves the planet (d^2 / 2R, d the distance to the camera, as
M_TerrainHorizon); its normal leans with the curvature. The normal (world space), the color and
the ground (texelGround) go to the pixel shader through vertex interpolators.

Pixel shader: the land keeps its color and its normal; where the ground interpolated between the
samples lies below the sea level (antialiased on the coast line), the sea: its color by its depth
(turquoise over the sand of the shallows, deep blue offshore), sine waves fixed in the world
whose normals fade into roughness with the size of a pixel (no tiling far away, a wider sun path),
the reflection of the sky (Fresnel, from the sky view LUT of the SkyAtmosphere) and the glint of
the sun in emissive, without the engine's specular: no Lumen reflection to trace. The lakes
and rivers stand at their level (HorizonWater, CoarseWater) with the look of the water of the tiles
(horizon_water_nodes.py), except under the tiles, which draw their own. The sea
square under the tiles reads its ground from the texels of level 0, bilinear, so that the near
and far seas match, and is drawn only where the core puts the ocean (masked: the bilinear share
of the sea texels at least one half). The land under the tiles lies deeper by a pixel depth
offset, so that the tiles stay in front of it (horizon_sea_hlsl.py).
The land is lit by the detail normals of its level (horizon_detail_normals.py), finer than its
grid, where they are ready: DetailNormals 0 gives back the normals of the grid.
ATerrainWorld (UTerrainHorizonClipmap) sets its parameters per level.

The asset belongs to the game project, not to this repository. Run the script once per project,
and again after updating the plugin, in the Unreal Editor (the Python Editor Script Plugin must
be enabled); it rebuilds an existing asset in place (the levels referencing it keep it):
    Tools > Execute Python Script... > terrain_unreal/Scripts/create_horizon_gpu_material.py
"""

import importlib
import math
import os
import sys
import types

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import horizon_detail_normals as detail  # noqa: E402  (after the path of this script)
import horizon_reveal as reveal  # noqa: E402
import horizon_sea_hlsl as sea_code  # noqa: E402
import horizon_water_nodes as water_nodes  # noqa: E402
import material_settings as settings  # noqa: E402
import temporary_terrain_look as look  # noqa: E402
detail = importlib.reload(detail)  # the modules of this checkout, even if run before
look = importlib.reload(look)
reveal = importlib.reload(reveal)
sea_code = importlib.reload(sea_code)
settings = importlib.reload(settings)
water_nodes = importlib.reload(water_nodes)

PACKAGE_PATH = "/Game/TerrainEngine"
ASSET_NAME = "M_TerrainHorizonGpu"
DEFAULT_TEXTURE = "/Engine/EngineResources/Black"
SCALARS = {
    "Cells": 256.0, "FirstSlotX": 0.0, "FirstSlotZ": 0.0, "CoarseX": 0.0, "CoarseZ": 0.0,
    "CoarseFirstX": 0.0, "CoarseFirstZ": 0.0, "HasCoarser": 0.0, "MorphCells": 32.0,
    "SkirtCm": 200.0, "DropCm": 100.0, "PlanetRadiusCm": 60_030_976.0 / (2.0 * math.pi) * 100.0,
    "PitchCm": 800.0, "DropCoveredCm": 0.0, "DropFadeCm": 0.0,
}
# The look of the sea, tunable on the instances: colors of the water (linear), the strength of the
# waves and the roughness of the sea between them (GGX alpha).
SEA_SCALARS = {"WaveOriginX": 0.0, "WaveOriginY": 0.0, "SeaWaveScale": 1.0, "SeaAlpha": 0.02}
# What the engine lights of the sea besides its body color (the material draws the reflection and
# the glint itself): its specular and roughness.
SHADE_SCALARS = {"SeaEngineSpecular": 0.0, "SeaEngineRoughness": 1.0}
SEA_COLORS = {
    "SeaDeepColor": (0.004, 0.024, 0.062), "SeaOpenColor": (0.008, 0.075, 0.11),
    "SeaShallowColor": (0.05, 0.30, 0.28),
}
# The land of level 0 lies BehindTilesCm deeper (pixel depth offset) up to BehindBandCm beyond the
# band the tiles always cover (DropCoveredCm), then less and less across BehindFadeCm: the tiles
# stay in front where both surfaces cross (horizon_sea_hlsl.py).
BEHIND_SCALARS = {"BehindTilesCm": 100.0, "BehindBandCm": 12800.0, "BehindFadeCm": 12800.0}

VERTEX_HLSL = r"""
int side = (int)Cells + 1;
int2 first = int2((int)FirstSlotX, (int)FirstSlotZ);
int2 coarseFirst = int2((int)CoarseFirstX, (int)CoarseFirstZ);
int2 inCoarser = int2((int)CoarseX, (int)CoarseZ);
int2 ik = int2(round(Local.xy));
float4 own = Tex.Load(int3((ik + first) % side, 0));
// The samples whose mean the coarser level shows here (Bridge/horizon_morph.hpp): the morph
// takes the heights and the ground of the coarser texels (ca, cb), the crests lifting them by
// their own cells, and the surface layers of this level's texels (a, b, the same materials).
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
// On the ocean the last float is the ground under the sea, below 0 (texelGround). The ground of
// the land gets 1 cm: land at the sea level (or the whole ocean without bHorizonSea) stays dry.
#define HORIZON_GROUND(t) ((t).a < 0.0 ? (t).a : max((t).r, 0.0) + 0.01)
float height = lerp(own.r, 0.5 * (ca.r + cb.r), alpha);
float ground = lerp(HORIZON_GROUND(own), 0.5 * (HORIZON_GROUND(ca) + HORIZON_GROUND(cb)), alpha);
float2 slope = lerp(own.gb, 0.5 * (ca.gb + cb.gb), alpha);
#undef HORIZON_GROUND
// The layers of the ground (the temporary terrain material): its packed surface (material and
// biome, Bridge/terrain_surface.hpp), morphed as the heights.
float4 la0, la1, la2, lb0, lb1, lb2;
float2 lc0, lc1, lc2;
Look.Layers(own.a, la0, lb0, lc0);
Look.Layers(a.a, la1, lb1, lc1);
Look.Layers(b.a, la2, lb2, lc2);
W0Out = lerp(la0, 0.5 * (la1 + la2), alpha);
W1Out = lerp(lb0, 0.5 * (lb1 + lb2), alpha);
W2Out = float4(lerp(lc0, 0.5 * (lc1 + lc2), alpha), 0.0, height / 1000.0);
// The drop below the tiles fades out across their band over level 0 (Bridge/terrain_surface.hpp:
// dropShare): no step at their outer edge, none on the levels 1 and up.
float fromCenter = max(abs(ik.x - Cells * 0.5), abs(ik.y - Cells * 0.5)) * PitchCm;
float z = height * 100.0 - DropCm * saturate((DropCoveredCm - fromCenter) / max(DropFadeCm, 1.0));
if (Kind.r > 0.5) { z -= SkirtCm; }
// The sea square under the tiles, at the sea level; the pixel shader reads its ground.
if (Kind.g > 0.5)
{
    z = 0.0; slope = float2(0.0, 0.0); ground = -1.0;
    W0Out = float4(0.0, 0.0, 0.0, 0.0); W1Out = float4(0.0, 1.0, 0.0, 0.0); W2Out = float4(0.0, 0.0, 0.0, 0.0);
}
z -= dot(CameraRel, CameraRel) / (2.0 * PlanetRadiusCm);
// The normal of the curved ground: its slopes, and the curvature that leans it away from the
// camera by d / R (the sun shines at another angle far away).
NormalOut = normalize(float3(-slope + CameraRel / PlanetRadiusCm, 1.0));
GroundOut = ground;
MorphOut = alpha;
return float3(0.0, 0.0, z);
"""

SHADE_HLSL = r"""
float3 sea = SeaBody;
EmissiveOut = SeaMask * (Reflection + Glint) * (1.0 - TintAmount);
NormalOut = normalize(lerp(LandNormal, SeaNormal, SeaMask));
RoughnessOut = lerp(LandRoughness, SeaEngineRoughness, SeaMask);
SpecularOut = lerp(0.5, SeaEngineSpecular, SeaMask);
return lerp(lerp(LandColor, sea, SeaMask), Tint, TintAmount);
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


def mask(material, source, x, y, channels="rg"):
    node = expression(material, unreal.MaterialExpressionComponentMask, x, y)
    for channel in "rgba":
        node.set_editor_property(channel, channel in channels)
    connect(source, "", node, "")
    return node


def world_position(material, offsets, x, y):
    node = expression(material, unreal.MaterialExpressionWorldPosition, x, y)
    node.set_editor_property("world_position_shader_offset", offsets)
    return node


def local_position(material, x, y):
    """The position in the space of the grid (cells), of the vertex or of the pixel."""
    node = expression(material, unreal.MaterialExpressionTransformPosition, x, y)
    node.set_editor_property("transform_source_type",
                             unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    node.set_editor_property("transform_type",
                             unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    return node


def helpers():
    """The helpers of this script that horizon_water_nodes.py builds its nodes with, handed over
    explicitly: Unreal runs a script in globals of its own, not as the module __main__."""
    return types.SimpleNamespace(expression=expression, connect=connect, custom_node=custom_node,
                                 texture_parameter=texture_parameter, mask=mask,
                                 world_position=world_position, local_position=local_position,
                                 interpolate=interpolate)


def parameters(material):
    """The textures and the scalar and vector parameters, shared by both shaders."""
    nodes = {"HorizonTexels": texture_parameter(material, "HorizonTexels", -400),
             "CoarseTexels": texture_parameter(material, "CoarseTexels", -250),
             "HorizonNormals": texture_parameter(material, "HorizonNormals", -1700),
             "CoarseNormals": texture_parameter(material, "CoarseNormals", -1550)}
    nodes.update(detail.scalars(material, detail.HORIZON_SCALARS, -1300, -1900))
    scalars = (list(SCALARS.items()) + list(SEA_SCALARS.items()) + list(SHADE_SCALARS.items())
               + list(BEHIND_SCALARS.items()) + list(reveal.SCALARS.items()))
    for index, (name, value) in enumerate(scalars):
        scalar = expression(material, unreal.MaterialExpressionScalarParameter, -1300,
                            300 + 60 * index)
        scalar.set_editor_property("parameter_name", name)
        scalar.set_editor_property("default_value", value)
        nodes[name] = scalar
    for index, (name, value) in enumerate(SEA_COLORS.items()):
        color = expression(material, unreal.MaterialExpressionVectorParameter, -1300,
                           -800 - 120 * index)
        color.set_editor_property("parameter_name", name)
        color.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
        nodes[name] = color
    nodes.update(water_nodes.parameters(material, helpers()))
    return nodes


def vertex_shader(material, nodes, kind):
    float4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
    custom = custom_node(
        material, -900, 0, "HorizonVertex", look.struct_hlsl() + VERTEX_HLSL,
        ["Tex", "Coarse", "Local", "Kind", "Viewer", "CameraRel"] + list(SCALARS),
        [("NormalOut", unreal.CustomMaterialOutputType.CMOT_FLOAT3),
         ("W0Out", float4), ("W1Out", float4), ("W2Out", float4),
         ("GroundOut", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
         ("MorphOut", unreal.CustomMaterialOutputType.CMOT_FLOAT1)],
        unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    connect(nodes["HorizonTexels"], "", custom, "Tex")
    connect(nodes["CoarseTexels"], "", custom, "Coarse")
    connect(expression(material, unreal.MaterialExpressionPreSkinnedPosition, -1600, -100), "",
            custom, "Local")
    connect(kind, "", custom, "Kind")
    viewer = local_position(material, -1600, 100)
    connect(expression(material, unreal.MaterialExpressionCameraPositionWS, -1800, 100), "",
            viewer, "")
    connect(mask(material, viewer, -1400, 100), "", custom, "Viewer")
    relative = world_position(
        material, unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE_NO_OFFSETS, -1600, 200)
    connect(mask(material, relative, -1400, 200), "", custom, "CameraRel")
    for name in SCALARS:
        connect(nodes[name], "", custom, name)
    return custom


def interpolate(material, custom, output, y):
    node = expression(material, unreal.MaterialExpressionVertexInterpolator, -500, y)
    connect(custom, output, node, "")
    return node


def sea_shader(material, nodes, kind, ground):
    """The sea of the pixel: its mask, normal, body color, glint and reflection direction."""
    names = ["Ground", "Kind", "LocalPos", "Tex", "CameraRel", "Time", "SunDir",
             "SunIlluminance", "SkyView", "Cells", "FirstSlotX", "FirstSlotZ", "PlanetRadiusCm"]
    names += list(SEA_SCALARS) + list(SEA_COLORS)
    float1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    float3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
    custom = custom_node(
        material, -200, -700, "HorizonSea", sea_code.sea_hlsl(), names,
        [("SeaMask", float1), ("SeaBody", float3), ("SeaNormal", float3), ("Glint", float3)],
        float3)
    connect(ground, "", custom, "Ground")
    connect(kind, "", custom, "Kind")
    pixel_local = local_position(material, -700, -900)
    connect(world_position(material, unreal.WorldPositionIncludedOffsets.WPT_DEFAULT, -900, -900),
            "", pixel_local, "")
    connect(pixel_local, "", custom, "LocalPos")
    connect(nodes["HorizonTexels"], "", custom, "Tex")
    connect(world_position(material, unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE,
                           -700, -1000), "", custom, "CameraRel")
    connect(expression(material, unreal.MaterialExpressionTime, -700, -1100), "", custom, "Time")
    connect(expression(material, unreal.MaterialExpressionSkyAtmosphereLightDirection, -700, -1200),
            "", custom, "SunDir")
    connect(expression(material, unreal.MaterialExpressionSkyAtmosphereLightIlluminance, -700,
                       -1300), "", custom, "SunIlluminance")
    # Unused by the code: it makes the material bind the sky view LUT the code samples.
    connect(expression(material, unreal.MaterialExpressionSkyAtmosphereViewLuminance, -700,
                       -1400), "", custom, "SkyView")
    for name in ["Cells", "FirstSlotX", "FirstSlotZ", "PlanetRadiusCm"] + list(SEA_SCALARS):
        connect(nodes[name], "", custom, name)
    for name in SEA_COLORS:
        connect(nodes[name], "", custom, name)
    return custom


def land_shader(material, vertex, normal):
    """The land of the pixel: the temporary terrain material without its near detail (only the
    mean color of each texture), as the far tiles."""
    float1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    float2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
    float3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
    weights = {f"W{index}": interpolate(material, vertex, f"W{index}Out", 400 + 100 * index)
               for index in range(3)}
    params = look.look_parameters(material, False, -2400, 1200)
    camera_relative = world_position(
        material, unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE, -900, 900)
    position = custom_node(material, -600, 900, "HorizonLandPosition", look.POSITION_HLSL,
                           ["CameraRel", "WaveOriginX", "WaveOriginY"], [("Dist", float1)], float2)
    connect(camera_relative, "", position, "CameraRel")
    altitude = mask(material, weights["W2"], -350, 800, "a")
    meters = expression(material, unreal.MaterialExpressionMultiply, -250, 800)
    meters.set_editor_property("const_b", 1000.0)
    connect(altitude, "", meters, "A")
    inputs = look.shade_inputs() + look.layer_inputs(False)
    shade = custom_node(material, -100, 600, "HorizonLand", look.shade_hlsl(False), inputs,
                        [("NormalOut", float3), ("RoughOut", float1)], float3)
    for source, output, name in ((weights["W0"], "", "W0"), (weights["W1"], "", "W1"),
                                 (weights["W2"], "", "W2"), (normal, "", "N"),
                                 (position, "", "P"), (position, "Dist", "Dist"),
                                 (meters, "", "Z")):
        connect(source, output, shade, name)
    for name in inputs:
        if name in params:
            connect(params[name], "", shade, name)
    return shade, position


def detail_normal(material, nodes, vertex):
    """The normal of the land: the normal of the grid, lit by the detail normals of the level."""
    node = detail.custom(material, -700, 1500, "HorizonDetailNormal", detail.HORIZON_HLSL,
                         detail.HORIZON_INPUTS)
    connect(interpolate(material, vertex, "NormalOut", 0), "", node, "N")
    connect(interpolate(material, vertex, "MorphOut", 1700), "", node, "Morph")
    local = local_position(material, -1000, 1500)
    connect(world_position(material, unreal.WorldPositionIncludedOffsets.WPT_DEFAULT, -1200,
                           1500), "", local, "")
    connect(local, "", node, "Local")
    connect(world_position(material, unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE,
                           -1000, 1600), "", node, "CameraRel")
    connect(nodes["HorizonNormals"], "", node, "Tex")
    connect(nodes["CoarseNormals"], "", node, "Coarse")
    for name in detail.HORIZON_INPUTS[6:]:
        connect(nodes[name], "", node, name)
    return node


def mask_and_depth(material, nodes, kind, ground, attributes):
    """The opacity mask (the sea square only where the core puts the ocean) and the depth offset
    of the land under the tiles (horizon_sea_hlsl.py)."""
    float1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    names = ["Kind", "LocalPos", "Tex", "Cells", "FirstSlotX", "FirstSlotZ"]
    square = custom_node(material, 400, -500, "HorizonSeaSquare", sea_code.SQUARE_HLSL, names, [],
                         float1)
    behind_names = ["Kind", "LocalPos", "Ground", "Cells", "PitchCm", "DropCoveredCm"]
    behind = custom_node(material, 400, -350, "HorizonBehindTiles", sea_code.BEHIND_HLSL,
                         behind_names + list(BEHIND_SCALARS), [], float1)
    local = local_position(material, 200, -450)
    connect(world_position(material, unreal.WorldPositionIncludedOffsets.WPT_DEFAULT, 0, -450),
            "", local, "")
    for node in (square, behind):
        connect(kind, "", node, "Kind")
        connect(local, "", node, "LocalPos")
    connect(nodes["HorizonTexels"], "", square, "Tex")
    for name in ["Cells", "FirstSlotX", "FirstSlotZ"]:
        connect(nodes[name], "", square, name)
    connect(ground, "", behind, "Ground")
    for name in ["Cells", "PitchCm", "DropCoveredCm"] + list(BEHIND_SCALARS):
        connect(nodes[name], "", behind, name)
    connect(square, "", attributes, "OpacityMask")
    connect(behind, "", attributes, "PixelDepthOffset")
    return square


def pixel_shader(material, nodes, vertex, kind):
    # The outputs go through one MakeMaterialAttributes node: Python cannot connect the pixel
    # depth offset of the material itself.
    attributes = expression(material, unreal.MaterialExpressionMakeMaterialAttributes, 800, 0)
    normal = detail_normal(material, nodes, vertex)
    ground = interpolate(material, vertex, "GroundOut", 250)
    sea = sea_shader(material, nodes, kind, ground)
    square = mask_and_depth(material, nodes, kind, ground, attributes)
    land, position = land_shader(material, vertex, normal)
    for name in ("WaveOriginX", "WaveOriginY"):
        connect(nodes[name], "", position, name)
    # The default output of a vector parameter is its RGB; its alpha is the amount of the tint.
    tint = expression(material, unreal.MaterialExpressionVectorParameter, 100, 300)
    tint.set_editor_property("parameter_name", "DebugTint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    float1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    float3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
    shade = custom_node(
        material, 400, 0, "HorizonShade", SHADE_HLSL,
        ["LandColor", "LandNormal", "LandRoughness", "Tint", "TintAmount", "Reflection", "SeaMask",
         "SeaBody", "SeaNormal", "Glint"] + list(SHADE_SCALARS),
        [("EmissiveOut", float3), ("NormalOut", float3), ("RoughnessOut", float1),
         ("SpecularOut", float1)], float3)
    for source, output, name in ((land, "", "LandColor"), (land, "NormalOut", "LandNormal"),
                                 (land, "RoughOut", "LandRoughness"), (tint, "", "Tint"),
                                 (tint, "A", "TintAmount"),
                                 (sea, "", "Reflection"), (sea, "SeaMask", "SeaMask"),
                                 (sea, "SeaBody", "SeaBody"),
                                 (sea, "SeaNormal", "SeaNormal"), (sea, "Glint", "Glint")):
        connect(source, output, shade, name)
    for name in SHADE_SCALARS:
        connect(nodes[name], "", shade, name)
    # The lakes and rivers at their level over the shaded ground (horizon_water_nodes.py).
    g = helpers()
    water = water_nodes.vertex(material, g, nodes, kind)
    inland = water_nodes.pixel(material, g, nodes, shade, water)
    water_nodes.opacity_mask(material, g, reveal.mask(material, g, nodes, square), inland,
                             attributes)
    for source, output, name in ((inland, "", "BaseColor"),
                                 (inland, "EmissiveOut", "EmissiveColor"),
                                 (inland, "NormalOut", "Normal"),
                                 (inland, "RoughnessOut", "Roughness"),
                                 (inland, "SpecularOut", "Specular"),
                                 (water_nodes.world_offset(material, g, vertex, water), "",
                                  "WorldPositionOffset")):
        connect(source, output, attributes, name)
    unreal.MaterialEditingLibrary.connect_material_property(
        attributes, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)


def create_horizon_gpu_material():
    asset_path = f"{PACKAGE_PATH}/{ASSET_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        material = unreal.EditorAssetLibrary.load_asset(asset_path)
        look.clear_material(material)
        unreal.log(f"{asset_path} exists: rebuilt in place")
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        material = tools.create_asset(ASSET_NAME, PACKAGE_PATH, unreal.Material,
                                      unreal.MaterialFactoryNew())
    # Every setting reset (material_settings.py), then: the normal of the pixel shader in world
    # space (the grids are flat, the scale is not); masked, the sea square cut where the core puts
    # no ocean.
    settings.reset_material(material, tangent_space_normal=False, use_material_attributes=True,
                            blend_mode=unreal.BlendMode.BLEND_MASKED, opacity_mask_clip_value=0.5)
    nodes = parameters(material)
    kind = expression(material, unreal.MaterialExpressionVertexColor, -1600, 0)
    vertex = vertex_shader(material, nodes, kind)
    pixel_shader(material, nodes, vertex, kind)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.log(f"{asset_path} created")
    return material


create_horizon_gpu_material()
