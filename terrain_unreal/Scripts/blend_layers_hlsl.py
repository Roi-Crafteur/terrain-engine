"""The blend layers of the TEMPORARY terrain material (phase 14, agent 29), for
temporary_terrain_hlsl.py, which pastes them into its helper struct (FTemporaryTerrain Look):

- AddBlend: adds one decoded blend texel (BlendTexel: 3 biome and 3 material ids, their weights)
  with its share, never interpolating its ids (biome_blend.hpp: addTexel);
- FinishBlend: the layers of the summed weights, with the overrides of the materials;
- BlendLayers: the 4 texels of a tile in the atlas (Bridge/tile_blend_atlas.hpp) around a point;
- TexelBlend: a texel of a blend texture of the horizon (Bridge/horizon_blend_texels.hpp);
- BlendTexelsLayers: up to 4 such texels with their shares (the morph of the horizon, the
  bilinear texels of level 0 under the tiles).

BIOME_A, BIOME_B, BIOME_C and TEXELS are filled by temporary_terrain_hlsl.struct_hlsl. No
identifier may be a type Unreal rewrites (half, fixed...) or a reserved word of HLSL (sample,
point, linear...): unreal_horizon_blend_scripts_test checks it, and every name declared here
against those of the struct and of the custom nodes it is pasted into.
"""

BLEND_HLSL = r"""
    // Adds one blend texel, its share times its weights (biome_blend.hpp: addTexel), never
    // interpolating its ids: the weight of each biome (wb0: biomes 0-3, wb1: 4-7, wb2: 8-11) and
    // of each material (wm0: materials 0-3, wm1: 4-7), added through masks rather than indexed
    // arrays (no array indexed at run time: no scratch memory on the GPU). ids, lo, hi: the ids
    // (bits 24-30: the share of the dunes of an erg in 127ths, agent 34) and the low and high 16
    // bits of the weights of a BlendTexel; wd: the shares times the share of the dunes.
    void AddBlend(uint ids, uint lo, uint hi, float share, inout float4 wb0, inout float4 wb1,
                  inout float4 wb2, inout float4 wm0, inout float4 wm1, inout float wd)
    {
        wd += share * (float)((ids >> 24u) & 127u) / 127.0;
        float3 wb = float3(lo & 255u, lo >> 8u, 0.0) / 255.0;
        wb.z = max(1.0 - wb.x - wb.y, 0.0);
        float3 wt = float3(hi & 255u, hi >> 8u, 0.0) / 255.0;
        wt.z = max(1.0 - wt.x - wt.y, 0.0);
        const float4 lanes = float4(0.0, 1.0, 2.0, 3.0);
        [unroll] for (int k = 0; k < 3; ++k)
        {
            float biome = (float)min((ids >> (4u * k)) & 15u, 11u);
            float material = (float)min((ids >> (12u + 4u * k)) & 15u, 7u);
            float w = share * wb[k];
            float wmk = share * wt[k];
            wb0 += w * (float4)(lanes == biome);
            wb1 += w * (float4)(lanes + 4.0 == biome);
            wb2 += w * (float4)(lanes + 8.0 == biome);
            wm0 += wmk * (float4)(lanes == material);
            wm1 += wmk * (float4)(lanes + 4.0 == material);
        }
    }

    // The layers of the summed blends (AddBlend): those of the biomes (BIOMES), with the
    // overrides of the materials (as in Layers), both linear in the weights; the weight of the
    // ocean and of the desert tell the river bed and the sand apart; the sand of the dunes is the
    // smooth sand, never the stony desert around it. wd, d: the share of the dunes of an erg
    // (biome_blend.hpp: its floor below 16 127ths, its dunes above).
    void FinishBlend(float4 wb0, float4 wb1, float4 wb2, float4 wm0, float4 wm1, float wd,
                     out float4 a, out float4 b, out float2 c, out float d)
    {
        d = saturate(wd);
        float dune = saturate((d - 16.0 / 127.0) / (1.0 - 16.0 / 127.0));
        const float4 BA[12] = { BIOME_A };
        const float4 BB[12] = { BIOME_B };
        const float2 BC[12] = { BIOME_C };
        float wbs[12] = { wb0.x, wb0.y, wb0.z, wb0.w, wb1.x, wb1.y, wb1.z, wb1.w,
                          wb2.x, wb2.y, wb2.z, wb2.w };
        float4 A = float4(0.0, 0.0, 0.0, 0.0);
        float4 B = A;
        c = float2(0.0, 0.0);
        [unroll] for (int n = 0; n < 12; ++n)
        {
            A += wbs[n] * BA[n]; B += wbs[n] * BB[n]; c += wbs[n] * BC[n];
        }
        float ocean = wb0.x;
        float desert = wb0.z;
        float4 bed = ocean * BB[0] + (1.0 - ocean) * float4(0.5, 0.0, 0.0, 0.5);       // river bed
        float soil = wm0.w + wm1.z;                                                      // dirt, mud
        float keep = wm0.z + wm1.y;                                                      // grass, snow
        a = keep * A + 0.3 * soil * A + wm0.x * ocean * BA[0];
        b = keep * B + soil * (0.3 * B + float4(0.7, 0.0, 0.0, 0.0)) + wm0.x * bed
          + wm0.y * lerp(float4(0.0, 1.0 - desert, desert, 0.0), float4(0.0, 1.0, 0.0, 0.0), dune)
          + (wm1.x + wm1.w) * float4(0.0, 0.0, 0.0, 1.0);
        c.x = lerp(c.x, 1.0, wm1.y);                                                     // snow
        c.y = lerp(c.y, 1.0, wm1.x);                                                     // rock
    }

    // The layers of the surface blend of a tile (Bridge/tile_blend_atlas.hpp): the 4 texels of
    // its slot of the atlas T around the point at local (centimeters from the lowest corner of the
    // tile), pitchCm apart, each adding its bilinear share (AddBlend), then FinishBlend; the share
    // of the dunes rides on the last float, 1 + 127ths / 256 (d). False where the tile has no
    // texels (slot -1, or the texels of no tile).
    bool BlendLayers(Texture2D T, float slot, float pitchCm, float2 local, out float4 a,
                     out float4 b, out float2 c, out float d)
    {
        a = float4(0.0, 0.0, 0.0, 0.0); b = a; c = float2(0.0, 0.0); d = 0.0;
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
        float4 wb0 = float4(0.0, 0.0, 0.0, 0.0);
        float4 wb1 = wb0, wb2 = wb0, wm0 = wb0, wm1 = wb0;
        float wd = 0.0;
        [unroll] for (int t = 0; t < 4; ++t)
        {
            float4 x = T.Load(int3(corner + int2(base) + int2(t & 1, t >> 1), 0));
            if (x.a < 0.5) { return false; }
            uint dunes = min((uint)(max(x.a - 1.0, 0.0) * 256.0 + 0.5), 127u);
            AddBlend((uint)(x.r + 0.5) | (dunes << 24u), (uint)(x.g + 0.5), (uint)(x.b + 0.5),
                     share[t], wb0, wb1, wb2, wm0, wm1, wd);
        }
        FinishBlend(wb0, wb1, wb2, wm0, wm1, wd, a, b, c, d);
        return true;
    }

    // A texel of a blend texture of the horizon (Bridge/horizon_blend_texels.hpp: 4 channels of
    // 16 bits, unsigned normalized, read without filtering): its ids and weights; false where it
    // holds no blend (the present bit unset: a texel never written).
    bool TexelBlend(float4 t, out uint ids, out uint lo, out uint hi)
    {
        uint4 v = (uint4)(saturate(t) * 65535.0 + 0.5);
        ids = v.x | ((v.y & 255u) << 16u) | (((v.y >> 8u) & 127u) << 24u);
        lo = v.z;
        hi = v.w;
        return (v.y & 32768u) != 0u;
    }

    // The layers of up to 4 blend texels of the horizon, each with its share (the shares sum to
    // 1): every texel decoded first, its weights added with its share (AddBlend), never its ids
    // interpolated, then FinishBlend (d: the share of the dunes). False, the layers and d
    // untouched, unless every texel of a share above 0 holds a blend.
    bool BlendTexelsLayers(float4 t0, float4 t1, float4 t2, float4 t3, float4 share,
                           inout float4 a, inout float4 b, inout float2 c, inout float d)
    {
        uint4 ids, lo, hi;
        bool present = TexelBlend(t0, ids.x, lo.x, hi.x) || share.x <= 0.0;
        present = (TexelBlend(t1, ids.y, lo.y, hi.y) || share.y <= 0.0) && present;
        present = (TexelBlend(t2, ids.z, lo.z, hi.z) || share.z <= 0.0) && present;
        present = (TexelBlend(t3, ids.w, lo.w, hi.w) || share.w <= 0.0) && present;
        if (!present) { return false; }
        float4 wb0 = float4(0.0, 0.0, 0.0, 0.0);
        float4 wb1 = wb0, wb2 = wb0, wm0 = wb0, wm1 = wb0;
        float wd = 0.0;
        // The texels of no share (the horizon away from the border of its window) are skipped.
        [unroll] for (int t = 0; t < 4; ++t)
        {
            BRANCH if (share[t] > 0.0)
            {
                AddBlend(ids[t], lo[t], hi[t], share[t], wb0, wb1, wb2, wm0, wm1, wd);
            }
        }
        FinishBlend(wb0, wb1, wb2, wm0, wm1, wd, a, b, c, d);
        return true;
    }

"""
