"""The vertex shader of the GPU horizon (M_TerrainHorizonGpu, create_horizon_gpu_material.py),
apart from the script that builds the material: the texel of the sample of each vertex, the
morph toward the coarser level, the layers of the ground, the drop, the skirt, the sea square and
the curvature of the planet (the head of create_horizon_gpu_material.py tells it).

Agent 40 added, each behind a switch of the material (1 by default; 0 gives back the horizon
before, to the bit; Terrain.Param to compare):
- AdaptiveSplit: the cells cut along their contour lines. Each cell of the grids holds a vertex
  of its own (Kind.b, Bridge/horizon_split.hpp: withCellCenters) standing on its corner (i + 1, k):
  there it is that sample and the cell keeps the fixed diagonal; moved onto the corner (i, k), the
  cell is cut along its main diagonal. A cell takes the diagonal whose ends have the nearer
  heights (|h00 - h11| < |h10 - h01|, mainDiagonal), and, past the middle of the morph, the one of
  the coarser cell where it lies along it (centerOnMain); the odd samples of both axes morph toward
  the mean of the diagonal of the coarser cell (coarserOnMain). A wall, a ridge or a coast line
  oblique to the grid no longer turns into a tooth per cell.
The vertex hands the sample it stands for (LocalOut: the mesh position, or the corner a center
moved onto) to the vertex node of the water (horizon_water_nodes.py), so that the water of a moved
center is that of its corner.
"""

# The helpers of the vertex shader, before its code (a struct: a Custom node takes no function).
GROUND_HLSL = r"""
struct FHorizonGround
{
    // Whether the cell whose corner (i, k) is the texel at (first + ik) is cut along its main
    // diagonal by its contour lines (Bridge/horizon_split.hpp: mainDiagonal, Contour).
    bool ContourMain(Texture2D T, int2 at, int side)
    {
        float h00 = T.Load(int3(at % side, 0)).r;
        float h10 = T.Load(int3((at + int2(1, 0)) % side, 0)).r;
        float h01 = T.Load(int3((at + int2(0, 1)) % side, 0)).r;
        float h11 = T.Load(int3((at + int2(1, 1)) % side, 0)).r;
        return abs(h00 - h11) < abs(h10 - h01);
    }
};
FHorizonGround Ground;
"""

VERTEX_HLSL = r"""
int side = (int)Cells + 1;
int2 first = int2((int)FirstSlotX, (int)FirstSlotZ);
int2 coarseFirst = int2((int)CoarseFirstX, (int)CoarseFirstZ);
int2 inCoarser = int2((int)CoarseX, (int)CoarseZ);
int2 ik = int2(round(Local.xy));
// The vertex of the center of a cell (Kind.b, Bridge/horizon_split.hpp: withCellCenters) stands
// on the corner (i + 1, k) of its cell, the fixed diagonal from (i + 1, k) to (i, k + 1): it is
// that sample, to the bit. With AdaptiveSplit (agent 40), it moves onto the corner (i, k) when the
// cell is cut along its main diagonal (centerOnMain): the diagonal along the contour lines, or,
// past the middle of the morph, the one the coarser cell is cut along.
float2 rel = CameraRel; // x and y, cm, from the camera
float shiftCm = 0.0;
BRANCH if (Kind.b > 0.5 && AdaptiveSplit > 0.5)
{
    int2 cell = ik - int2(1, 0);
    bool onMain = Ground.ContourMain(Tex, cell + first, side);
    float centerDistance = max(abs(cell.x + 0.5 - Viewer.x), abs(cell.y + 0.5 - Viewer.y));
    float centerMorph = HasCoarser > 0.5
        ? saturate((centerDistance - (Cells * 0.5 - 2.0 - MorphCells)) / MorphCells) : 0.0;
    if (centerMorph >= 0.5)
    {
        bool coarseMain = Ground.ContourMain(Coarse, inCoarser + cell / 2 + coarseFirst, side);
        bool alongMain = (cell.x & 1) == (cell.y & 1);
        if (coarseMain == alongMain) { onMain = coarseMain; }
    }
    if (onMain) { ik = cell; shiftCm = -PitchCm; rel.x -= PitchCm; }
}
LocalOut = float2(ik); // the sample of the vertex, for the water (horizon_water_nodes.py)
float4 own = Tex.Load(int3((ik + first) % side, 0));
// The samples whose mean the coarser level shows here (Bridge/horizon_morph.hpp): the morph
// takes the heights and the ground of the coarser texels (ca, cb), the crests lifting them by
// their own cells, and the surface layers of this level's texels (a, b, the same materials).
int2 s0 = ik;
int2 s1 = ik;
bool oddI = (ik.x & 1) != 0;
bool oddK = (ik.y & 1) != 0;
if (oddI && oddK)
{
    s0 = ik + int2(1, -1);
    s1 = ik + int2(-1, 1);
    // The mean of the diagonal the coarser cell is cut along (coarserOnMain): its own with
    // AdaptiveSplit, the fixed one without.
    BRANCH if (AdaptiveSplit > 0.5 &&
               Ground.ContourMain(Coarse, inCoarser + (ik - int2(1, 1)) / 2 + coarseFirst, side))
    {
        s0 = ik - int2(1, 1);
        s1 = ik + int2(1, 1);
    }
}
else if (oddI) { s0 = ik - int2(1, 0); s1 = ik + int2(1, 0); }
else if (oddK) { s0 = ik - int2(0, 1); s1 = ik + int2(0, 1); }
float4 a = Tex.Load(int3((s0 + first) % side, 0));
float4 b = Tex.Load(int3((s1 + first) % side, 0));
float4 ca = Coarse.Load(int3((inCoarser + s0 / 2 + coarseFirst) % side, 0));
float4 cb = Coarse.Load(int3((inCoarser + s1 / 2 + coarseFirst) % side, 0));
float distance = max(abs(ik.x - Viewer.x), abs(ik.y - Viewer.y));
float alpha = HasCoarser > 0.5 ? saturate((distance - (Cells * 0.5 - 2.0 - MorphCells)) / MorphCells) : 0.0;
// On the ocean the last float is the ground under the sea, below 0 (texelGround), SEA_ROCK lower
// over bare rock (Bridge/horizon_textures.hpp: kSeaRockCode). The ground of the land gets 1 cm:
// land at the sea level (or the whole ocean without bHorizonSea) stays dry.
#define HORIZON_GROUND(t) ((t).a < 0.0 ? SEA_GROUND((t).a) : max((t).r, 0.0) + 0.01)
float height = lerp(own.r, 0.5 * (ca.r + cb.r), alpha);
float ground = lerp(HORIZON_GROUND(own), 0.5 * (HORIZON_GROUND(ca) + HORIZON_GROUND(cb)), alpha);
float2 slope = lerp(own.gb, 0.5 * (ca.gb + cb.gb), alpha);
#undef HORIZON_GROUND
// The layers of the ground (the temporary terrain material): the surface blends of the samples
// (Bridge/horizon_blend_texels.hpp, agent 29), read without filtering, each decoded and added
// with its share of the morph ((1 - alpha) for this sample, alpha / 2 for each of the two whose
// mean the coarser level shows), never their ids interpolated: the weights still sum to 1. Where
// a sample has no blend yet (a texel never written), its packed surface (material and biome,
// Bridge/terrain_surface.hpp), morphed as the heights.
float4 wa = float4(0.0, 0.0, 0.0, 0.0);
float4 wb = wa;
float2 wc = float2(0.0, 0.0);
float wd = 0.0; // the share of the dunes of an erg (agent 34), 0 without blends
bool blended = false;
BRANCH if (SurfaceBlends > 0.5)
{
    float4 bOwn = Blend.Load(int3((ik + first) % side, 0));
    float4 bA = Blend.Load(int3((s0 + first) % side, 0));
    float4 bB = Blend.Load(int3((s1 + first) % side, 0));
    float4 shares = float4(1.0 - alpha, 0.5 * alpha, 0.5 * alpha, 0.0);
    blended = Look.BlendTexelsLayers(bOwn, bA, bB, bOwn, shares, wa, wb, wc, wd);
}
BRANCH if (!blended)
{
    float4 la0, la1, la2, lb0, lb1, lb2;
    float2 lc0, lc1, lc2;
    Look.Layers(own.a, la0, lb0, lc0);
    Look.Layers(a.a, la1, lb1, lc1);
    Look.Layers(b.a, la2, lb2, lc2);
    wa = lerp(la0, 0.5 * (la1 + la2), alpha);
    wb = lerp(lb0, 0.5 * (lb1 + lb2), alpha);
    wc = lerp(lc0, 0.5 * (lc1 + lc2), alpha);
}
W0Out = wa;
W1Out = wb;
// W2.z: the share of the dunes (the foot of the steep slopes of the tiles has no use here).
W2Out = float4(wc, wd, height / 1000.0);
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
z -= dot(rel, rel) / (2.0 * PlanetRadiusCm);
// The normal of the curved ground: its slopes, and the curvature that leans it away from the
// camera by d / R (the sun shines at another angle far away).
NormalOut = normalize(float3(-slope + rel / PlanetRadiusCm, 1.0));
GroundOut = ground;
MorphOut = alpha;
return float3(shiftCm, 0.0, z);
"""


def vertex_hlsl(sea_ground):
    """The code of the vertex node: the helpers, then the shader, the ground under the sea decoded
    by sea_ground (temporary_terrain_hlsl.sea_ground_hlsl of "(t).a")."""
    return GROUND_HLSL + VERTEX_HLSL.replace("SEA_GROUND((t).a)", sea_ground)
