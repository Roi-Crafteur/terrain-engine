"""The detail normals of the far horizon in the materials (Bridge/horizon_normal_stream.hpp).

Shared by create_horizon_gpu_material.py (the levels of the horizon) and
create_temporary_terrain_material.py (the tiles, from level 0). Per level, a texture of
(Cells + 1) * NormalRatio texels per side holds the slopes of the ground (two half floats) on a
lattice NormalRatio times finer than the samples: texel (a, b) of the sample in slot s lies at
s * NormalRatio + (a, b), and stands at (i + a / NormalRatio, k + b / NormalRatio) cells from the
low corner of the window for the sample (i, k) of the window. Bilinear, wrapped as the slots.

A texel not computed yet holds 65504 on both channels (kNormalPendingHalf): wherever a filtered
slope exceeds 2000, the normal of the grid stays. The detail fades out where a pixel covers more
than one texel (no aliasing far away or at grazing angles: there the grid already shows the relief
at the size of a pixel), and DetailNormals (1) scales it: 0 gives back the normals of the grids,
as before the detail, for A/B comparisons.
"""

import unreal

FLOAT3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3

# The helpers: Read (the slope at fine coordinates f of a texture of `side` texels per side, its
# first slot `first`), Pending (a pending texel within the filter) and Weight (the share of the
# detail for a pixel covering `foot` texels).
STRUCT_HLSL = r"""
struct FHorizonDetail
{
    float2 Read(Texture2D T, SamplerState S, float2 f, float2 first, float ratio, float side)
    {
        return T.SampleLevel(S, (f + first * ratio + 0.5) / side, 0.0).rg;
    }
    bool Pending(float2 g) { return max(abs(g.x), abs(g.y)) > 2000.0; }
    float Weight(float2 f)
    {
        float foot = sqrt(length(ddx(f)) * length(ddy(f)));
        return saturate(1.0 - 0.5 * log2(max(foot, 1.0)));
    }
};
FHorizonDetail Detail;
"""

# The horizon: N the normal of the grid (with the curvature), Local the position of the pixel in
# cells of its level, Morph the geomorphing factor of the vertex toward the coarser level, whose
# detail it then reads (its window CoarseX, CoarseZ cells from the low corner of the coarser one).
HORIZON_HLSL = STRUCT_HLSL + r"""
float3 n = normalize(N);
float2 f = Local.xy * NormalRatio;
float w = DetailNormals * Detail.Weight(f); // derivatives outside of any branch
BRANCH if (HasNormals < 0.5 || w <= 0.0) { return n; }
float side = (Cells + 1.0) * NormalRatio;
float2 g = Detail.Read(Tex, TexSampler, f, float2(FirstSlotX, FirstSlotZ), NormalRatio, side);
BRANCH if (Morph > 0.001)
{
    float2 fc = (float2(CoarseX, CoarseZ) + Local.xy * 0.5) * NormalRatio;
    float2 gc = Detail.Read(Coarse, CoarseSampler, fc, float2(CoarseFirstX, CoarseFirstZ), NormalRatio, side);
    g = Detail.Pending(gc) ? g : lerp(g, gc, Morph);
}
if (Detail.Pending(g)) { return n; }
float3 d = normalize(float3(-g + CameraRel.xy / PlanetRadiusCm, 1.0));
return normalize(lerp(n, d, w));
"""

HORIZON_INPUTS = ["N", "Local", "Morph", "Tex", "Coarse", "CameraRel", "Cells", "FirstSlotX",
                  "FirstSlotZ", "CoarseX", "CoarseZ", "CoarseFirstX", "CoarseFirstZ",
                  "PlanetRadiusCm", "NormalRatio", "HasNormals", "DetailNormals"]

# The tiles: Rel the position of the pixel from the low corner of level 0 (cm), the slope there
# from its detail, or the smooth slope of its samples (W3.xy) while pending.
TILE_SLOPE_HLSL = STRUCT_HLSL + r"""
float2 f = Rel.xy / SurfacePitchCm * SurfaceNormalRatio;
float w = DetailNormals * Detail.Weight(f); // derivatives outside of any branch
BRANCH if (SurfaceHasNormals < 0.5 || w <= 0.0) { return float3(Smooth, 0.0); }
float side = (SurfaceCells + 1.0) * SurfaceNormalRatio;
float2 g = Detail.Read(Tex, TexSampler, f, float2(SurfaceFirstX, SurfaceFirstZ), SurfaceNormalRatio, side);
if (Detail.Pending(g)) { return float3(Smooth, 0.0); }
return float3(lerp(Smooth, g, w), 1.0);
"""

TILE_INPUTS = ["Smooth", "Rel", "Tex", "SurfaceCells", "SurfacePitchCm", "SurfaceFirstX",
               "SurfaceFirstZ", "SurfaceNormalRatio", "SurfaceHasNormals", "DetailNormals"]

# Scalars of the horizon material and of the tile material.
HORIZON_SCALARS = {"NormalRatio": 4.0, "HasNormals": 0.0, "DetailNormals": 1.0}
TILE_SCALARS = {"SurfaceNormalRatio": 4.0, "SurfaceHasNormals": 0.0, "DetailNormals": 1.0}


def custom(material, x, y, description, code, inputs):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", FLOAT3)
    node.set_editor_property("description", description)
    custom_inputs = []
    for name in inputs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    node.set_editor_property("inputs", custom_inputs)
    return node


def texture(material, name, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureObjectParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("texture", unreal.EditorAssetLibrary.load_asset(
        "/Engine/EngineResources/Black"))
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return node


def scalars(material, values, x, y):
    nodes = {}
    for index, (name, value) in enumerate(values.items()):
        node = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionScalarParameter, x, y + 60 * index)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        nodes[name] = node
    return nodes
