"""The HLSL of the lakes and rivers of the GPU horizon (agent 27), for horizon_water_nodes.py.

- WATER_VERTEX_HLSL (HorizonWaterVertex, a vertex node beside HorizonVertex, whose code it leaves
  alone): the water texels of the level and of the next coarser one (inputs Levels and
  CoarseLevels, the parameters HorizonWater and CoarseWater: the level of the core, R32F,
  Bridge/horizon_textures.hpp), morphed as the heights (the same
  samples and factor as HorizonVertex). Over a lake or a river (a level above -1e8 and land: the
  ocean, its texel below 0, is the sea's), the surface stands at the level of the water, never
  below the ground; the node returns how far above the ground the vertex rises (added to the world
  position offset of HorizonVertex, so the same drop and curvature apply), and hands the pixels
  the depth of the water (-1 m on dry land: the shore lies where it crosses 0), its altitude and
  its kind (1 river where the core says the ground is water, 3 salt lake in a desert, 2 a lake).
  Where the tiles cover level 0 (within DropCoveredCm of its center) nothing rises: the tiles draw
  their own water there, and the horizon keeps its ground under them; the water rises from the
  edge of the tiles, exactly at the level their water has there.
- INLAND_HLSL (HorizonInlandWater, after HorizonShade): the water over the ground of the pixel,
  the same look as the water of the tiles (water_look_hlsl.py) — the bed seen through its depth
  along the view, the color of deep water by its kind and place, ripples, the reflection of the
  sky and the glint of the sun — antialiased on the shore. The cells across the edge of the tiles,
  half raised, are cut (KeepOut, the opacity mask): the tiles cover them.
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
float height = lerp(t.r, 0.5 * (ta.r + tb.r), alpha);
float surface = lerp(SURFACE(t, w), 0.5 * (SURFACE(ta, wa) + SURFACE(tb, wb)), alpha);
float depth = lerp(DEPTH(t, w), 0.5 * (DEPTH(ta, wa) + DEPTH(tb, wb)), alpha);
// The kind for the color: the packed surface (material + 8 biome + 128 under inland water,
// Bridge/terrain_surface.hpp) of the first wet sample.
float code = INLAND(t, w) ? t.a : (INLAND(ta, wa) ? ta.a : tb.a);
#undef INLAND
#undef SURFACE
#undef DEPTH
int packed = (int)(code + 0.5) % 128;
InlandKindOut = packed % 8 == 0 ? 1.0 : (packed / 8 == 2 ? 3.0 : 2.0);
float fromCenter = max(abs(ik.x - Cells * 0.5), abs(ik.y - Cells * 0.5)) * PitchCm;
float open = fromCenter >= DropCoveredCm - 1.0 ? 1.0 : 0.0;
bool drawn = InlandWater > 0.5 && Kind.r < 0.5 && Kind.g < 0.5;
InlandOpenOut = open;
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
KeepOut = mask > 0.0 && Open < 0.999 ? 0.0 : 1.0;
// The ripples fixed in the world, from the origin of the core; the size of a pixel, outside of
// any branch.
const float PERIOD = 409600.0; // 4096 m, cm
float2 p = (GetTranslatedWorldPosition(Parameters).xy - GetPeriodicWorldOrigin(float3(PERIOD, PERIOD, PERIOD)).xy - float2(WaveOriginX, WaveOriginY)) * 0.01;
float footprint = max(length(ddx(p)), length(ddy(p))) + 1e-3;
float3 color = Color;
BRANCH
if (mask > 0.0 && Open >= 0.999)
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
    // The water along the view down to the bed, as the tiles measure it behind their pixels.
    float thickness = max(Depth, 0.0) / max(V.z, 0.05);
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
