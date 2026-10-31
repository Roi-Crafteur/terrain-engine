"""The reveal of the levels of the GPU horizon (agent 38), for create_horizon_gpu_material.py.

A new place (the start, a teleport) comes level by level, finest first: each level comes in
dithered as it is shown (Reveal from 0 to 1, set by TerrainHorizonClipmap.cpp), and the coarsest
level shown while the coarser ones are still to come dissolves its outer EdgeFadeCells cells into
the sky (EdgeFade 1), so that the land never ends on a sharp line. Reveal 1 and EdgeFade 0 once
every level is in: the horizon as before.
"""

import unreal

SCALARS = {"Reveal": 1.0, "EdgeFade": 0.0, "EdgeFadeCells": 24.0}
INPUTS = ["LocalPos", "Cells"] + list(SCALARS)

# The share of a pixel of the level kept, dithered on the screen and in time (the temporal
# antialiasing blends it): Reveal, times the outer band of the window where EdgeFade dissolves it.
# LocalPos is the position in the cells of the window, from its low corner.
HLSL = r"""
float half = Cells * 0.5;
float2 p = LocalPos.xy;
float d = max(abs(p.x - half), abs(p.y - half));
float edge = saturate((half - d) / max(EdgeFadeCells, 1.0));
float keep = saturate(Reveal) * lerp(1.0, edge, saturate(EdgeFade));
float2 s = Parameters.SvPosition.xy + (float)View.StateFrameIndexMod8 * float2(5.588238, 5.588238);
float noise = frac(52.9829189 * frac(dot(s, float2(0.06711056, 0.00583715))));
return keep > noise ? 1.0 : 0.0;
"""


def mask(material, g, nodes, square):
    """The opacity mask of the sea square (square), times the reveal of the level. g: the helpers
    of create_horizon_gpu_material.py (helpers())."""
    float1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    reveal = g.custom_node(material, 400, -650, "HorizonReveal", HLSL, INPUTS, [], float1)
    local = g.local_position(material, 200, -650)
    g.connect(g.world_position(material, unreal.WorldPositionIncludedOffsets.WPT_DEFAULT, 0,
                               -650), "", local, "")
    g.connect(local, "", reveal, "LocalPos")
    for name in INPUTS[1:]:
        g.connect(nodes[name], "", reveal, name)
    revealed = g.expression(material, unreal.MaterialExpressionMultiply, 550, -600)
    g.connect(square, "", revealed, "A")
    g.connect(reveal, "", revealed, "B")
    return revealed
