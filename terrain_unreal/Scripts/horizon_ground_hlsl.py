"""The foot of the steep slopes and the curvature of the ground on the GPU horizon (agent 40,
M_TerrainHorizonGpu, create_horizon_gpu_material.py): the material of the ground reads them to
lay gravel in the hollows and at the foot of slopes and bare earth on bumps and crests
(temporary_terrain_hlsl.py: FOOT, CURVATURE). The tiles compute them at their vertices from the
texels of level 0 (create_temporary_terrain_material.py); the horizon had neither, and the arc at
512 m where the tiles hand over showed it.

The horizon computes them in its pixel shader, with the formulas of the tiles at the point of the
pixel: the foot from the steepest of the 4 samples of its cell (rise over run: 0 under 0.7, 1 from
1.2), the curvature as the Laplacian of the heights (bilinear) over 2 samples on each side at level
0 (16 m, as the tiles) and 1 beyond (1/m), so that both sides of the edge of the tiles read the
same values; across the morph, toward those of the coarser level. Not in the vertex shader: its
interpolators are all taken (coast_rock_scripts_test.py, 22 scalars). HorizonFootCurv 0 gives 0
for both, the horizon before agent 40, to the bit.
"""

import unreal

# The caps of the rock (agent 40, RockCaps; 0: none, as before): the cornice at the top of a face
# (its share and its height, Bridge/horizon_textures.hpp: kCapFloats, from the bands of the core),
# read by the pixel shader from the texels of the four samples around it, bilinear, as the bands
# (create_horizon_gpu_material.py: BANDS_HLSL), and handed to the rock (Look.Bands, input C).
CAP_INPUTS = ["CapTex", "LocalPos", "Cells", "FirstSlotX", "FirstSlotZ", "RockCaps"]
CAPS_HLSL = r"""
BRANCH if (RockCaps < 0.5)
{
    return float2(0.0, 0.0);
}
int side = (int)Cells + 1;
int2 first = int2((int)FirstSlotX, (int)FirstSlotZ);
float2 p = clamp(LocalPos.xy, 0.0, Cells - 0.001);
int2 i0 = int2(floor(p));
float2 t = p - float2(i0);
float2 c00 = CapTex.Load(int3((i0 + first) % side, 0)).rg;
float2 c10 = CapTex.Load(int3((i0 + int2(1, 0) + first) % side, 0)).rg;
float2 c01 = CapTex.Load(int3((i0 + int2(0, 1) + first) % side, 0)).rg;
float2 c11 = CapTex.Load(int3((i0 + int2(1, 1) + first) % side, 0)).rg;
return lerp(lerp(c00, c10, t.x), lerp(c01, c11, t.x), t.y);
"""

# The inputs of the node, in this order.
INPUTS = ["Tex", "Coarse", "LocalPos", "Morph", "Cells", "PitchCm", "FirstSlotX", "FirstSlotZ",
          "CoarseX", "CoarseZ", "CoarseFirstX", "CoarseFirstZ", "HasCoarser", "HorizonFootCurv"]

# The tiles start the foot at this steepness and take it whole this much steeper (rise over run).
FOOT_FROM = 0.7
FOOT_RAMP = 0.5

FOOT_CURV_HLSL = r"""
struct FHorizonFootCurv
{
    // The height (meters) at ij of a window of texels, bilinear between its 4 samples, as the
    // tiles read it (temporary_terrain_hlsl.py: Height).
    float Height(Texture2D T, float2 ij, int2 first, int side, float cells)
    {
        float2 base = clamp(floor(ij), 0.0, cells - 1.0);
        float2 f = saturate(ij - base);
        int2 i0 = int2(base);
        float h00 = T.Load(int3((i0 + first) % side, 0)).r;
        float h10 = T.Load(int3((i0 + int2(1, 0) + first) % side, 0)).r;
        float h01 = T.Load(int3((i0 + int2(0, 1) + first) % side, 0)).r;
        float h11 = T.Load(int3((i0 + int2(1, 1) + first) % side, 0)).r;
        return lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
    }

    // The foot and the curvature at ij (the tiles at their vertices: the steepest of the samples
    // of the cell holding it; the Laplacian over reach samples on each side, spanM meters).
    float2 At(Texture2D T, float2 ij, int2 first, int side, float cells, float reach, float spanM)
    {
        int2 i0 = int2(clamp(floor(ij), 0.0, cells - 1.0));
        float2 s00 = T.Load(int3((i0 + first) % side, 0)).gb;
        float2 s10 = T.Load(int3((i0 + int2(1, 0) + first) % side, 0)).gb;
        float2 s01 = T.Load(int3((i0 + int2(0, 1) + first) % side, 0)).gb;
        float2 s11 = T.Load(int3((i0 + int2(1, 1) + first) % side, 0)).gb;
        float steep = max(max(length(s00), length(s10)), max(length(s01), length(s11)));
        float lap = Height(T, ij + float2(reach, 0.0), first, side, cells)
                  + Height(T, ij - float2(reach, 0.0), first, side, cells)
                  + Height(T, ij + float2(0.0, reach), first, side, cells)
                  + Height(T, ij - float2(0.0, reach), first, side, cells)
                  - 4.0 * Height(T, ij, first, side, cells);
        return float2(saturate((steep - FOOT_FROM) / FOOT_RAMP), lap / (spanM * spanM));
    }
};
FHorizonFootCurv FootCurv;
float2 result = float2(0.0, 0.0);
BRANCH if (HorizonFootCurv > 0.5)
{
    int side = (int)Cells + 1;
    float reach = PitchCm < 1000.0 ? 2.0 : 1.0; // 16 m at level 0, as the tiles; one sample beyond
    result = FootCurv.At(Tex, LocalPos.xy, int2((int)FirstSlotX, (int)FirstSlotZ), side, Cells,
                         reach, reach * PitchCm / 100.0);
    BRANCH if (Morph > 0.0 && HasCoarser > 0.5)
    {
        float2 coarse = FootCurv.At(Coarse, LocalPos.xy * 0.5 + float2(CoarseX, CoarseZ),
                                    int2((int)CoarseFirstX, (int)CoarseFirstZ), side, Cells, 1.0,
                                    2.0 * PitchCm / 100.0);
        result = lerp(result, coarse, Morph);
    }
}
return result;
"""


def foot_curv_hlsl():
    """The code of the node, its constants those of the tiles."""
    return FOOT_CURV_HLSL.replace("FOOT_FROM", f"{FOOT_FROM:.1f}").replace(
        "FOOT_RAMP", f"{FOOT_RAMP:.1f}")


def node(material, g, nodes, morph):
    """HorizonFootCurv under the pixel, from the texels of the level and of the coarser level; g:
    the helpers of create_horizon_gpu_material.py, morph: its interpolated MorphOut."""
    custom = g.custom_node(material, -600, 2050, "HorizonFootCurv", foot_curv_hlsl(), INPUTS, [],
                           unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    local = g.local_position(material, -800, 2100)
    g.connect(g.world_position(material, unreal.WorldPositionIncludedOffsets.WPT_DEFAULT, -1000,
                               2100), "", local, "")
    for source, name in ((nodes["HorizonTexels"], "Tex"), (nodes["CoarseTexels"], "Coarse"),
                         (local, "LocalPos"), (morph, "Morph")):
        g.connect(source, "", custom, name)
    for name in INPUTS[4:]:
        g.connect(nodes[name], "", custom, name)
    return custom


def caps_texture(material, g):
    """HorizonCaps: read without filtering, on a sampler shared with the world (no slot of its
    own), as HorizonBands."""
    node = g.texture_parameter(material, "HorizonCaps", -2150)
    node.set_editor_property("sampler_source",
                             unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
    return node


def caps(material, g, nodes):
    """HorizonCaps under the pixel: the cap of the rock and its height, its switch RockCaps."""
    custom = g.custom_node(material, -600, 1950, "HorizonCaps", CAPS_HLSL, CAP_INPUTS, [],
                           unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    local = g.local_position(material, -800, 2000)
    g.connect(g.world_position(material, unreal.WorldPositionIncludedOffsets.WPT_DEFAULT, -1000,
                               2000), "", local, "")
    switch = g.expression(material, unreal.MaterialExpressionScalarParameter, -800, 1950)
    switch.set_editor_property("parameter_name", "RockCaps")
    switch.set_editor_property("default_value", 1.0)
    for source, name in ((nodes["HorizonCaps"], "CapTex"), (local, "LocalPos"),
                         (switch, "RockCaps")):
        g.connect(source, "", custom, name)
    for name in ("Cells", "FirstSlotX", "FirstSlotZ"):
        g.connect(nodes[name], "", custom, name)
    return custom
