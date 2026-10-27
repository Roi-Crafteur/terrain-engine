"""The HLSL of the TEMPORARY terrain material (phase 14), for temporary_terrain_look.py.

Custom nodes cannot declare functions: the helpers live in a struct (FTemporaryTerrain Look).
- Layers: the ground layers of a texel of the horizon (its packed surface, terrain_surface.hpp).
- BlendLayers: the ground layers of the surface blend of a tile (its texels in the atlas,
  Bridge/tile_blend_atlas.hpp): the weights of 3 biomes and 3 materials per texel.
- Noise: periodic value noise in world space (4096 m period, as the waves of the sea).
- Bomb: stochastic tiling, each cell of 0.7 texture repeat with its own offset and quarter turn,
  blended over the last quarter of the cell: no repetition can be read near or far.
- RockPlane: one plane of the triplanar rock, two rock textures mixed by world patches.
"""

# Texels along each side of a tile (terrain_core stream_manager.hpp: kTileBlendTexels).
TILE_BLEND_TEXELS = 5

STRUCT_HLSL = r"""
struct FTemporaryTerrain
{
    // The layers of a texel: its packed surface (material + 8 biome, plus 128 under a lake or a
    // river, Bridge/terrain_surface.hpp) or, on the ocean, a negative ground.
    void Layers(float code, out float4 a, out float4 b, out float2 c)
    {
        const float4 BA[12] = { BIOME_A };
        const float4 BB[12] = { BIOME_B };
        const float2 BC[12] = { BIOME_C };
        int v = code < 0.0 ? 0 : (int)(code + 0.5) % 128;
        int m = v % 8;
        int biome = min(v / 8, 11);
        a = BA[biome]; b = BB[biome]; c = BC[biome];
        float4 z = float4(0.0, 0.0, 0.0, 0.0);
        if (m == 0 && biome != 0) { a = z; b = float4(0.5, 0.0, 0.0, 0.5); }           // river bed
        else if (m == 1) { a = z; b = biome == 2 ? float4(0, 0, 1, 0) : float4(0, 1, 0, 0); }
        else if (m == 3 || m == 6) { a *= 0.3; b = b * 0.3 + float4(0.7, 0.0, 0.0, 0.0); } // dirt, mud
        else if (m == 4) { a = z; b = float4(0.0, 0.0, 0.0, 1.0); c.y = 1.0; }            // rock
        else if (m == 5) { c.x = 1.0; }                                                    // snow
        else if (m == 7) { a = z; b = float4(0.0, 0.0, 0.0, 1.0); }                        // scree
    }

    // The layers of the surface blend of a tile (Bridge/tile_blend_atlas.hpp): the 4 texels of
    // its slot of the atlas T around the point at local (centimeters from the lowest corner of the
    // tile), pitchCm apart. Each slot of each texel adds its bilinear share of its weight to its
    // id (biome_blend.hpp: addTexel), never interpolating the ids; the layers are those of every
    // biome (BIOMES) and the overrides of every material (as in Layers), both linear in the
    // weights. False where the tile has no texels (slot -1, or the texels of no tile).
    bool BlendLayers(Texture2D T, float slot, float pitchCm, float2 local, out float4 a,
                     out float4 b, out float2 c)
    {
        const float4 BA[12] = { BIOME_A };
        const float4 BB[12] = { BIOME_B };
        const float2 BC[12] = { BIOME_C };
        a = float4(0.0, 0.0, 0.0, 0.0); b = a; c = float2(0.0, 0.0);
        if (slot < -0.5) { return false; }
        uint width, height;
        T.GetDimensions(width, height);
        int perSide = max((int)width / TEXELS, 1);
        int s = (int)(slot + 0.5);
        int2 corner = int2(s % perSide, s / perSide) * TEXELS;
        float2 ij = clamp(local / max(pitchCm, 1.0), 0.0, TEXELS - 1.0);
        float2 base = min(floor(ij), TEXELS - 2.0);
        float2 f = ij - base;
        float4 share = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
        float wm[8] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        float ocean = 0.0;
        float desert = 0.0;
        [unroll] for (int t = 0; t < 4; ++t)
        {
            float4 x = T.Load(int3(corner + int2(base) + int2(t & 1, t >> 1), 0));
            if (x.a < 0.5) { return false; }
            uint ids = (uint)(x.r + 0.5);
            uint lo = (uint)(x.g + 0.5);
            uint hi = (uint)(x.b + 0.5);
            float3 wb = float3(lo & 255u, lo >> 8u, 0.0) / 255.0;
            wb.z = max(1.0 - wb.x - wb.y, 0.0);
            float3 wt = float3(hi & 255u, hi >> 8u, 0.0) / 255.0;
            wt.z = max(1.0 - wt.x - wt.y, 0.0);
            [unroll] for (int k = 0; k < 3; ++k)
            {
                int biome = min((int)((ids >> (4u * k)) & 15u), 11);
                int material = min((int)((ids >> (12u + 4u * k)) & 15u), 7);
                float w = share[t] * wb[k];
                a += w * BA[biome]; b += w * BB[biome]; c += w * BC[biome];
                ocean += biome == 0 ? w : 0.0;
                desert += biome == 2 ? w : 0.0;
                wm[material] += share[t] * wt[k];
            }
        }
        float4 A = a, B = b;
        float4 z = float4(0.0, 0.0, 0.0, 0.0);
        float4 bed = ocean * BB[0] + (1.0 - ocean) * float4(0.5, 0.0, 0.0, 0.5);       // river bed
        float soil = wm[3] + wm[6];                                                      // dirt, mud
        float keep = wm[2] + wm[5];                                                      // grass, snow
        a = keep * A + 0.3 * soil * A + wm[0] * ocean * BA[0];
        b = keep * B + soil * (0.3 * B + float4(0.7, 0.0, 0.0, 0.0)) + wm[0] * bed
          + wm[1] * float4(0.0, 1.0 - desert, desert, 0.0) + (wm[4] + wm[7]) * float4(0.0, 0.0, 0.0, 1.0);
        c.x = lerp(c.x, 1.0, wm[5]);                                                     // snow
        c.y = lerp(c.y, 1.0, wm[4]);                                                     // rock
        return true;
    }

    // The height of the ground (meters) at the sample coordinates ij of the window of level 0,
    // bilinear between its 4 texels (the tiles: the vertex shader of create_temporary_terrain_
    // material.py; first, side: the toroidal window).
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

    float2 Hash2(float2 c)
    {
        c = fmod(fmod(c, 4096.0) + 4096.0, 4096.0);
        return frac(sin(float2(dot(c, float2(127.1, 311.7)), dot(c, float2(269.5, 183.3)))) * 43758.5453);
    }

    // Periodic value noise in [0, 1]: p in meters, cells of `cell` meters dividing 4096.
    float Noise(float2 p, float cell)
    {
        float n = 4096.0 / cell;
        float2 q = p / cell;
        float2 i = floor(q);
        float2 f = q - i;
        f = f * f * (3.0 - 2.0 * f);
        float4 h;
        float2 c00 = fmod(fmod(i, n) + n, n);
        float2 c11 = fmod(c00 + 1.0, n);
        h.x = frac(sin(dot(c00, float2(127.1, 311.7))) * 43758.5453);
        h.y = frac(sin(dot(float2(c11.x, c00.y), float2(127.1, 311.7))) * 43758.5453);
        h.z = frac(sin(dot(float2(c00.x, c11.y), float2(127.1, 311.7))) * 43758.5453);
        h.w = frac(sin(dot(c11, float2(127.1, 311.7))) * 43758.5453);
        return lerp(lerp(h.x, h.y, f.x), lerp(h.z, h.w, f.x), f.y);
    }

    // Two octaves of the noise for the patches, on lattices turned by 45 and 27 degrees (integer
    // matrices: still periodic) so that their edges never follow the axes; q is already warped.
    float Patch(float2 q, float cell, float offset)
    {
        float2 r1 = float2(q.x + q.y, q.y - q.x);
        float2 r2 = float2(2.0 * q.x + q.y, 2.0 * q.y - q.x);
        return Noise(r1 + offset, cell * 2.0) * 0.6 + Noise(r2 + offset * 3.0, cell * 8.0) * 0.4;
    }

    float3 Saturate(float3 c, float s)
    {
        float l = dot(c, float3(0.2126, 0.7152, 0.0722));
        return max(lerp(l.xxx, c, s), 0.0);
    }

    // Stochastic tiling: uv in texture repeats, dx and dy its derivatives. The normal (tangent
    // space, world aligned) comes from the dominant cell only.
    float3 Bomb(Texture2D A, SamplerState SA, Texture2D Nm, SamplerState SN, bool withNormal,
                float2 uv, float2 dx, float2 dy, out float2 nrm)
    {
        float2 q = uv / 0.7;
        float2 c = floor(q);
        float2 w = smoothstep(0.75, 1.0, q - c);
        float3 albedo = float3(0.0, 0.0, 0.0);
        float best = -1.0;
        float2 bestUV = uv;
        float2x2 bestR = float2x2(1.0, 0.0, 0.0, 1.0);
        [unroll] for (int i = 0; i < 4; ++i)
        {
            float2 o = float2(i & 1, i >> 1);
            float wt = lerp(1.0 - w.x, w.x, o.x) * lerp(1.0 - w.y, w.y, o.y);
            BRANCH if (wt > 0.01)
            {
                float2 h = Hash2(c + o);
                float k = floor(h.x * 4.0);
                float2 cs = k < 1.0 ? float2(1.0, 0.0) : (k < 2.0 ? float2(0.0, 1.0) :
                            (k < 3.0 ? float2(-1.0, 0.0) : float2(0.0, -1.0)));
                float2x2 R = float2x2(cs.x, -cs.y, cs.y, cs.x);
                float2 u = mul(R, uv) + h * 37.0;
                albedo += wt * A.SampleGrad(SA, u, mul(R, dx), mul(R, dy)).rgb;
                if (wt > best) { best = wt; bestUV = u; bestR = R; }
            }
        }
        nrm = float2(0.0, 0.0);
        BRANCH if (withNormal)
        {
            float2 t = Nm.SampleGrad(SN, bestUV, mul(bestR, dx), mul(bestR, dy)).rg * 2.0 - 1.0;
            nrm = mul(transpose(bestR), t);
        }
        return albedo;
    }

    // One plane of the triplanar rock: the rock texture and its variant, mixed by `alt`.
    float3 RockPlane(Texture2D A, SamplerState SA, Texture2D NA, SamplerState SNA,
                     Texture2D B, SamplerState SB, Texture2D NB, SamplerState SNB,
                     float3 tintA, float3 tintB, float alt, float2 uv, float2 dx, float2 dy,
                     out float2 nrm)
    {
        float3 c = float3(0.0, 0.0, 0.0);
        float2 n;
        nrm = float2(0.0, 0.0);
        BRANCH if (alt < 0.99)
        {
            c += (1.0 - alt) * tintA * Bomb(A, SA, NA, SNA, true, uv, dx, dy, n);
            nrm += (1.0 - alt) * n;
        }
        BRANCH if (alt > 0.01)
        {
            c += alt * tintB * Bomb(B, SB, NB, SNB, true, uv * 1.3, dx * 1.3, dy * 1.3, n);
            nrm += alt * n;
        }
        return c;
    }
};
FTemporaryTerrain Look;
"""

# The world position in meters, periodic as the waves of the sea (4096 m, which divides each side
# of the wrapped world; WaveOriginX/Y follow the origin rebasing), and the distance to the camera.
POSITION_HLSL = r"""
const float PERIOD = 409600.0;
float2 w = GetTranslatedWorldPosition(Parameters).xy - GetPeriodicWorldOrigin(float3(PERIOD, PERIOD, PERIOD)).xy - float2(WaveOriginX, WaveOriginY);
Dist = length(CameraRel) / 100.0;
return w / 100.0;
"""

# The pixel shader of the ground (custom node): from the layer weights W0, W1, W2 (W2: cold, rock
# of the core, foot of steep slopes, altitude), the world normal N, P the world position in meters
# (periodic), Z the altitude in meters and Dist the distance to the camera: the base color
# (returned), the world normal and the roughness.
SHADE_TOP = r"""
float3 P3 = float3(P, Z);
float3 dPx = ddx(P3);
float3 dPy = ddy(P3);
float detail = HAS_DETAIL * saturate((DetailFarM - Dist) / max(DetailFarM - DetailNearM, 1.0));
float n1 = Look.Noise(P, 32.0);
float n2 = Look.Noise(P, 256.0);
float n3 = Look.Noise(P, 8.0);
float n4 = Look.Noise(P + 211.0, 512.0);
float slope = degrees(acos(saturate(N.z)));
float jit = (n1 - 0.5) * 8.0 + (n3 - 0.5) * 3.0;
float2 warp = float2(Look.Noise(P + 7.0, 16.0), Look.Noise(P - 7.0, 16.0)) - 0.5;
float2 Q = P + warp * 10.0;
float pd = Look.Patch(Q, 4.0, 47.0);
// Sharp transitions in patches, not a uniform half-and-half mix: rock and ground, snow and rock
// meet along ragged edges (a blend of a dark rock and a white snow would read as sand).
float rock = saturate((slope - (RockStartDeg + jit)) / max(RockFullDeg - RockStartDeg, 1.0));
rock = smoothstep(0.3, 0.7, rock + (pd - 0.5) * 0.6);
rock = max(rock, W2.y * CoreRock);
float cold = saturate((W2.x - 0.5) * 2.5 + (n2 - 0.5) * 1.2 + 0.5);
float hold = saturate((SnowShedDeg + jit - slope) / max(SnowShedDeg - SnowHoldDeg, 1.0));
hold = smoothstep(0.35, 0.65, hold + (pd - 0.5) * 0.8);
float snow = cold * hold;
// Above the snow line nothing grows: where the slope sheds the snow, the rock shows.
rock = max(rock, cold * (1.0 - hold) * 0.95);
float g[8] = { W0.x, W0.y, W0.z, W0.w, W1.x, W1.y, W1.z, W1.w };
// Patches within a biome, as in Minecraft: dry grass and darker grass in the meadows, rock
// outcrops in the snow, scree in the rock; soft but readable edges, from a few meters to tens of
// meters.
float pb = Look.Patch(Q, 8.0, 93.0);
float pc = Look.Patch(Q, 8.0, 29.0);
// Bare earth and gravel: rare ragged patches that follow the relief, not discs at random. Gravel
// collects in the hollows (CURVATURE > 0: the ground curves up, 1/m) and at the foot of slopes;
// bare earth shows on bumps and crests (CURVATURE < 0) and on moderate slopes, where the soil is
// thin. Their masks: the site of each (PatchRelief of it) mixed with a doubly warped noise of
// three scales, above a high threshold (PatchRarity); ragged edges (a fine noise, faded far away
// where it would shimmer). Without a site, no patch.
float curv = CURVATURE * CurvatureGain;
float hollow = saturate(curv);
float bump = saturate(-curv);
float incline = saturate((slope - 6.0) / 16.0);
float2 R = Q + (float2(Look.Noise(P + 31.0, 64.0), Look.Noise(P - 31.0, 64.0)) - 0.5) * 24.0;
float rag = (Look.Noise(R, 2.0) - 0.5) * 0.06 * saturate((600.0 - Dist) / 300.0);
float ma = Look.Patch(R, 8.0, 17.0) * 0.7 + Look.Noise(R + 13.0, 128.0) * 0.2
         + Look.Noise(R + 3.0, 4.0) * 0.1 + rag;
float mb = Look.Patch(R, 8.0, 71.0) * 0.7 + Look.Noise(R - 13.0, 128.0) * 0.2
         + Look.Noise(R - 3.0, 4.0) * 0.1 + rag;
float bareSite = saturate(bump + 0.6 * incline);
float gravelSite = saturate(hollow + 0.6 * saturate(W2.z) + 0.25 * incline);
float bareMask = lerp(ma, bareSite, PatchRelief) - PatchRarity;
float gravelMask = lerp(mb, gravelSite, PatchRelief) - PatchRarity;
float bare = PatchAmount * smoothstep(0.0, 0.05, bareMask);
float gravel = PatchAmount * smoothstep(0.0, 0.05, gravelMask);
// The desert keeps more of its gravel, still in the hollows first.
float desertGravel = PatchAmount * smoothstep(0.0, 0.05, gravelMask + 0.12);
float dryp = PatchAmount * smoothstep(0.60, 0.68, pb);
float dark = PatchAmount * (1.0 - smoothstep(0.32, 0.40, pb));
float outcrop = PatchAmount * smoothstep(0.66, 0.74, pc);
float meadow = g[0];
g[1] += meadow * dryp * 0.8;
g[2] += meadow * dark * 0.6;
g[0] = meadow * (1.0 - dryp * 0.8 - dark * 0.6);
[unroll] for (int v = 0; v < 4; ++v)
{
    g[4] += g[v] * bare * 0.75;
    g[7] += g[v] * gravel * 0.6;
    g[v] *= 1.0 - bare * 0.75 - gravel * 0.6;
}
g[7] += g[6] * desertGravel * 0.5;
g[6] *= 1.0 - desertGravel * 0.5;
snow *= 1.0 - outcrop * 0.85;
rock = max(rock * (1.0 - gravel * 0.5), outcrop * cold * 0.9);
float alt = smoothstep(0.42, 0.58, Look.Patch(Q, 16.0, 61.0));
// No sand on the mountains: above the alpine zone (SandMaxAltitudeM) and on slopes steeper than
// 30 degrees, the sand and the desert of any biome become scree.
float sandless = max(saturate((Z - SandMaxAltitudeM) / 300.0), saturate((slope - 30.0) / 5.0));
g[7] += (g[5] + g[6]) * sandless;
g[5] *= 1.0 - sandless;
g[6] *= 1.0 - sandless;
float sandShare = g[5] + g[6];
// Moderate slopes show their soil: dirt under the grass and the forest, scree under the alpine
// grass.
float soil = 0.6 * saturate((slope - 26.0 - jit) / 10.0);
float lowland = g[0] + g[1] + g[2];
float alpine = g[3];
float foot = 0.7 * saturate(W2.z) * (1.0 - rock);
float sum = 0.0;
[unroll] for (int s = 0; s < 8; ++s)
{
    g[s] *= (s < 4 ? 1.0 - soil : 1.0) * (1.0 - foot);
    sum += g[s];
}
g[4] += lowland * soil * (1.0 - foot);
g[7] += alpine * soil * (1.0 - foot) + foot;
sum += (lowland + alpine) * soil * (1.0 - foot) + foot;
if (sum < 1e-3) { g[4] = 1.0; sum = 1.0; }
float3 ground = float3(0.0, 0.0, 0.0);
float2 nts = float2(0.0, 0.0);
// What shows of the ground under the rock and the snow: nothing to sample when it is covered.
float cover = (1.0 - rock) * (1.0 - snow);
"""

LAYER_FAR = r"""
BRANCH if (g[I] > 0.01 && cover > 0.02)
{
    float3 c = TAI.SampleLevel(TAISampler, P / SCALE, 13.0).rgb;
DETAIL_CODELAYER_SATURATION    ground += (g[I] / sum) * c * TintI;
}
"""

LAYER_DETAIL = r"""    BRANCH if (detail > 0.0)
    {
        float2 nl;
        float3 cd = Look.Bomb(TAI, TAISampler, TNI, TNISampler, g[I] > 0.35 * sum, P / SCALE, dPx.xy / SCALE, dPy.xy / SCALE, nl);
        c = lerp(c, cd, detail);
        nts += (g[I] / sum) * nl;
    }
"""

ROCK_FAR = r"""
float3 rockC = lerp(TAR.SampleLevel(TARSampler, P / SCALE, 13.0).rgb * TintR,
                    TAV.SampleLevel(TAVSampler, P / SCALE, 13.0).rgb * TintV, alt);
float3 rockN = N;
float3 rockMean = rockC;
"""

ROCK_DETAIL = r"""
BRANCH if (rock > 0.01 && detail > 0.0 && snow < 0.98)
{
    float3 bw = pow(abs(N), 8.0);
    bw = max(bw - 0.15 * max(bw.x, max(bw.y, bw.z)), 0.0);
    bw /= dot(bw, float3(1.0, 1.0, 1.0));
    float3 c3 = float3(0.0, 0.0, 0.0);
    float3 dn = float3(0.0, 0.0, 0.0);
    float2 t;
    if (bw.x > 0.05) { c3 += bw.x * Look.RockPlane(ROCK_ARGS, P3.yz / SCALE, dPx.yz / SCALE, dPy.yz / SCALE, t); dn += bw.x * float3(0.0, t.x, t.y); }
    if (bw.y > 0.05) { c3 += bw.y * Look.RockPlane(ROCK_ARGS, P3.xz / SCALE, dPx.xz / SCALE, dPy.xz / SCALE, t); dn += bw.y * float3(t.x, 0.0, t.y); }
    if (bw.z > 0.05) { c3 += bw.z * Look.RockPlane(ROCK_ARGS, P3.xy / SCALE, dPx.xy / SCALE, dPy.xy / SCALE, t); dn += bw.z * float3(t.x, t.y, 0.0); }
    rockC = lerp(rockC, c3, detail);
    rockN = normalize(N + dn * NormalStrength * detail);
}
"""

SNOW_DETAIL = r"""
BRANCH if (snow > 0.01 && detail > 0.0)
{
    float2 unused;
    snowC = lerp(snowC, Look.Bomb(TAS, TASSampler, TAS, TASSampler, false, P / SCALE, dPx.xy / SCALE, dPy.xy / SCALE, unused), detail);
}
"""

SHADE_BOTTOM = r"""
// The ground takes the saturation, a large-scale variation of tone and hue (50 to 500 m) and the
// warmth; the rock is a cold grey mineral (its textures nearly desaturated, their light and dark
// kept and strengthened: cracks and strata, albedo about 0.15) and the snow a clean, slightly cold
// white with a sheen: under a low warm sun neither may read as sand.
float macro = 1.0 + MacroVariation * (0.22 * (n2 - 0.5) + 0.12 * (n1 - 0.5));
float3 hue = lerp(float3(1.0, 1.0, 1.0), lerp(float3(1.07, 1.02, 0.86), float3(0.9, 1.0, 1.08), n4), MacroVariation);
float3 groundC = Look.Saturate(ground * macro * hue, Saturation) * Warmth;
float3 rockS = max(lerp(rockMean, rockC, RockContrast), 0.0);
float3 rockG = Look.Saturate(rockS, 1.0 - RockGrey) * RockAlbedo * (1.0 + 0.4 * MacroVariation * (n2 - 0.5));
float3 snowG = lerp(snowC, float3(0.88, 0.91, 0.96), SnowWhiten) * TintS;
float3 groundN = normalize(float3(N.xy + nts * NormalStrength * detail, N.z));
float3 color = lerp(lerp(groundC, rockG, rock), snowG, snow);
float3 normal = normalize(lerp(groundN, rockN, rock));
normal = normalize(lerp(normal, N, snow * 0.7));
RoughOut = lerp(lerp(0.92, 0.8, rock), 0.4, snow);
if (DebugView > 0.5)
{
    const float3 K[8] = { float3(0.1, 0.6, 0.1), float3(0.8, 0.7, 0.2), float3(0.0, 0.25, 0.05),
        float3(0.2, 0.7, 0.6), float3(0.45, 0.25, 0.1), float3(0.9, 0.8, 0.6), float3(1.0, 0.45, 0.1),
        float3(0.5, 0.5, 0.5) };
    float3 d = float3(0.0, 0.0, 0.0);
    [unroll] for (int k = 0; k < 8; ++k) { d += (g[k] / sum) * K[k]; }
    d = lerp(lerp(d, float3(0.9, 0.1, 0.1), rock), float3(1.0, 1.0, 1.0), snow);
    color = DebugView < 1.5 ? d : (DebugView < 2.5 ? float3(rock, snow, saturate(W2.z)) :
            (DebugView < 3.5 ? float3(sandShare / max(sum, 1e-3), 0.0, 0.0) :
            (DebugView < 4.5 ? float3(hollow, 0.15, bump) : float3(bare, gravel, 0.0))));
}
NormalOut = normal;
return color;
"""


def struct_hlsl(biomes):
    """The helper struct, its biome tables filled from the biomes of the look."""
    def rows(index, width):
        values = []
        for biome in biomes:
            cells = ", ".join(f"{float(x):.2f}" for x in biome[index])
            values.append(f"float{width}({cells})")
        return ", ".join(values)
    return (STRUCT_HLSL.replace("BIOME_A", rows(0, 4)).replace("BIOME_B", rows(1, 4))
            .replace("BIOME_C", rows(2, 2)).replace("TEXELS", str(TILE_BLEND_TEXELS)))


def ground_layer(index, scale, detail, saturation):
    """The code of one ground layer: its mean color, or its stochastic texture near the camera."""
    layer = LAYER_FAR.replace("DETAIL_CODE", LAYER_DETAIL if detail else "")
    layer = layer.replace("LAYER_SATURATION", "" if saturation is None else
                          f"    c = Look.Saturate(c, {saturation:.2f});\n")
    return layer.replace("TAI", f"TA{index}").replace("TNI", f"TN{index}").replace(
        "TintI", f"Tint{index}").replace("[I]", f"[{index}]").replace("SCALE", f"{scale:.1f}")


def shade_hlsl(detail, layers, ground, saturations, biomes):
    """The pixel code: with detail (tiles), the textures near the camera; without (horizon), only
    the mean color of each texture. layers: (name, albedo, normal, mean, scale); the ground layers
    first, then the snow, the rock and its variant."""
    # The curvature of the ground comes from the vertex shader of the tiles (W3.w); the horizon
    # has none.
    code = SHADE_TOP.replace("HAS_DETAIL", "1.0" if detail else "0.0").replace(
        "CURVATURE", "W3.w" if detail else "0.0")
    for index in range(ground):
        code += ground_layer(index, layers[index][4], detail, saturations.get(layers[index][0]))
    snow, rock, variant = ground, ground + 1, ground + 2
    scale = f"{layers[rock][4]:.1f}"
    code += ROCK_FAR.replace("TAR", f"TA{rock}").replace("TAV", f"TA{variant}").replace(
        "TintR", f"Tint{rock}").replace("TintV", f"Tint{variant}").replace("SCALE", scale)
    code += (f"float3 snowC = TA{snow}.SampleLevel(TA{snow}Sampler, P / {layers[snow][4]:.1f}, "
             "13.0).rgb;\n")
    if detail:
        args = (f"TA{rock}, TA{rock}Sampler, TN{rock}, TN{rock}Sampler, TA{variant}, "
                f"TA{variant}Sampler, TN{variant}, TN{variant}Sampler, Tint{rock}, Tint{variant}, alt")
        code += ROCK_DETAIL.replace("ROCK_ARGS", args).replace("SCALE", scale)
        code += SNOW_DETAIL.replace("TAS", f"TA{snow}").replace("SCALE", f"{layers[snow][4]:.1f}")
    code += SHADE_BOTTOM.replace("TintS", f"Tint{snow}")
    return struct_hlsl(biomes) + code
