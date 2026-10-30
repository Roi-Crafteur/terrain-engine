"""The HLSL of the lakes and rivers of the GPU horizon (agent 27), for horizon_water_nodes.py.

- WATER_VERTEX_HLSL (HorizonWaterVertex, a vertex node beside HorizonVertex, whose code it leaves
  alone): the water texels of the level and of the next coarser one (inputs Levels and
  CoarseLevels, the parameters HorizonWater and CoarseWater: the level of the core, R32F,
  Bridge/horizon_textures.hpp), morphed as the heights (the same
  samples and factor as HorizonVertex). Over a lake or a river (a level above -1e8 and land: the
  ocean, its texel below 0, is the sea's), the surface stands at the level of the water, never
  below the ground; the node returns how far above the ground the vertex rises (added to the world
  position offset of HorizonVertex, so the same drop and curvature apply), and hands the pixels
  the depth of the water, its altitude and its kind (1 river where the core says the ground is
  water, 3 salt lake in a desert, 2 a lake). A dry sample beside the water takes for depth the
  level of its wettest neighbor minus its ground (-1 m away from the water): the shore lies where
  the depth crosses 0, on the line where the ground of the horizon meets the water, not on the
  diagonals of its cells. Where the tiles cover level 0 nothing rises (they draw their own water,
  and the horizon keeps its ground under them) but over the last DropFadeCm before the edge of
  the area they cover (DropCoveredCm from the center of level 0), where their water fades out:
  the water rises there (one cell more, so that no cell of the band is half raised), and the
  node hands the pixels how far from the center of level 0 they stand.
- INLAND_HLSL (HorizonInlandWater, after HorizonShade): the water over the ground of the pixel,
  the same look as the water of the tiles (water_look_hlsl.py) — the bed seen through its depth
  along the view, the color of deep water by its kind and place, ripples, the reflection of the
  sky and the glint of the sun — antialiased on the shore, faded over ShoreFadeM of water along the
  view as on the tiles. Over the outer band of the tiles, a pixel of water is kept (KeepOut, the
  opacity mask) with the share the water of the tiles loses there (FTerrainWater::HorizonShare),
  dithered in space and time (resolved by the temporal antialiasing); inside, it is cut: the
  tiles draw their water alone.
"""

WATER_VERTEX_HLSL = r"""
int side = (int)Cells + 1;
int2 first = int2((int)FirstSlotX, (int)FirstSlotZ);
int2 coarseFirst = int2((int)CoarseFirstX, (int)CoarseFirstZ);
int2 inCoarser = int2((int)CoarseX, (int)CoarseZ);
int2 ik = int2(round(Local.xy));
// The samples whose mean the coarser level shows here, as HorizonVertex (Bridge/horizon_morph.hpp).
int2 s0 = ik;
int2 s1 = ik;
bool oddI = (ik.x & 1) != 0;
bool oddK = (ik.y & 1) != 0;
if (oddI && oddK) { s0 = ik + int2(1, -1); s1 = ik + int2(-1, 1); }
else if (oddI) { s0 = ik - int2(1, 0); s1 = ik + int2(1, 0); }
else if (oddK) { s0 = ik - int2(0, 1); s1 = ik + int2(0, 1); }
int3 own = int3((ik + first) % side, 0);
int3 c0 = int3((inCoarser + s0 / 2 + coarseFirst) % side, 0);
int3 c1 = int3((inCoarser + s1 / 2 + coarseFirst) % side, 0);
float4 t = Tex.Load(own);
float4 ta = Coarse.Load(c0);
float4 tb = Coarse.Load(c1);
float w = Levels.Load(own).r;
float wa = CoarseLevels.Load(c0).r;
float wb = CoarseLevels.Load(c1).r;
float distance = max(abs(ik.x - Viewer.x), abs(ik.y - Viewer.y));
float alpha = HasCoarser > 0.5 ? saturate((distance - (Cells * 0.5 - 2.0 - MorphCells)) / MorphCells) : 0.0;
#define INLAND(tx, wl) ((wl) > -1.0e8 && (tx).a >= 0.0)
#define SURFACE(tx, wl) (INLAND(tx, wl) ? max((tx).r, (wl)) : (tx).r)
#define DEPTH(tx, wl) (INLAND(tx, wl) ? (wl) - (tx).r : -1.0)
// A dry sample beside the water: the level of its wettest neighbor over its ground.
float beside = -1.0e9;
for (int n = 0; n < 4; ++n)
{
    int2 d = n == 0 ? int2(1, 0) : (n == 1 ? int2(-1, 0) : (n == 2 ? int2(0, 1) : int2(0, -1)));
    beside = max(beside, Levels.Load(int3((ik + d + first + side) % side, 0)).r);
}
float shore = beside > -1.0e8 ? min(beside - t.r, -0.05) : -1.0;
float height = lerp(t.r, 0.5 * (ta.r + tb.r), alpha);
float surface = lerp(SURFACE(t, w), 0.5 * (SURFACE(ta, wa) + SURFACE(tb, wb)), alpha);
float depth = lerp(INLAND(t, w) ? w - t.r : shore, 0.5 * (DEPTH(ta, wa) + DEPTH(tb, wb)), alpha);
// The kind for the color: the packed surface (material + 8 biome + 128 under inland water,
// Bridge/terrain_surface.hpp) of the first wet sample.
float code = INLAND(t, w) ? t.a : (INLAND(ta, wa) ? ta.a : tb.a);
#undef INLAND
#undef SURFACE
#undef DEPTH
int packed = (int)(code + 0.5) % 128;
InlandKindOut = packed % 8 == 0 ? 1.0 : (packed / 8 == 2 ? 3.0 : 2.0);
float fromCenter = max(abs(ik.x - Cells * 0.5), abs(ik.y - Cells * 0.5)) * PitchCm;
float open = fromCenter >= DropCoveredCm - DropFadeCm - PitchCm - 1.0 ? 1.0 : 0.0;
bool drawn = InlandWater > 0.5 && Kind.r < 0.5 && Kind.g < 0.5;
InlandFromCenterOut = fromCenter;
InlandDepthOut = drawn ? depth : -1.0;
InlandAltitudeOut = surface;
return float3(0.0, 0.0, drawn ? (surface - height) * 100.0 * open : 0.0);
"""

INLAND_HLSL = r"""
EmissiveOut = Emissive;
NormalOut = Normal;
RoughnessOut = Roughness;
SpecularOut = Specular;
// The shore, where the depth interpolated between a wet and a dry sample crosses 0.
float mask = saturate(0.5 + Depth / max(fwidth(Depth), 1e-3));
// Over the outer band of the tiles, the share of the water the horizon draws (the tiles draw the
// rest, water_look_hlsl.py), a pixel kept with that share: interleaved gradient noise, moved every
// 1/60 s.
float share = saturate((FromCenter - (DropCoveredCm - DropFadeCm)) / max(DropFadeCm, 1.0));
float2 pixel = Parameters.SvPosition.xy + 5.588238 * fmod(floor(Time * 60.0), 64.0);
float dither = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
KeepOut = mask > 0.0 && share <= dither ? 0.0 : 1.0;
// The ripples fixed in the world, from the origin of the core; the size of a pixel, outside of
// any branch.
const float PERIOD = 409600.0; // 4096 m, cm
float2 p = (GetTranslatedWorldPosition(Parameters).xy - GetPeriodicWorldOrigin(float3(PERIOD, PERIOD, PERIOD)).xy - float2(WaveOriginX, WaveOriginY)) * 0.01;
float footprint = max(length(ddx(p)), length(ddy(p))) + 1e-3;
float3 color = Color;
BRANCH
if (mask > 0.0 && share > 0.0)
{
    float filtered;
    float2 ripples = Water.Waves(p, Time, footprint, filtered) * RippleStrength;
    float alpha2 = WaterAlpha * WaterAlpha + filtered * RippleStrength * RippleStrength;
    float3 V = normalize(-CameraRel);
    float3 n = normalize(float3(-ripples + CameraRel.xy / PlanetRadiusCm, 1.0));
    float Fresnel;
    float3 reflection;
    float3 Glint;
SKY_CODE
    // The water along the view down to the bed, as the tiles measure it behind their pixels;
    // faded out at the shore as theirs.
    float thickness = max(Depth, 0.0) / max(V.z, 0.05);
    mask *= saturate(thickness / max(ShoreFadeM, 0.01));
    float Kind = round(KindCode);
    float3 deep, absorb;
    BODY_CODE
    float through = dot(exp(-absorb * thickness), float3(0.3333, 0.3333, 0.3334));
    float3 water = (Color * through + deep * (1.0 - through)) * (1.0 - Fresnel);
    color = lerp(Color, water, mask);
    EmissiveOut = lerp(Emissive, reflection + Glint, mask);
    NormalOut = normalize(lerp(Normal, n, mask));
    RoughnessOut = lerp(Roughness, 1.0, mask);
    SpecularOut = lerp(Specular, 0.0, mask);
}
return color;
"""


def inland_hlsl(sky, body):
    """The code of the pixel of the inland water, its sky and body from water_look_hlsl.py."""
    return INLAND_HLSL.replace("SKY_CODE", sky).replace("BODY_CODE", body)
