"""The blend layers of the TEMPORARY terrain material (phase 14, agent 29), for
temporary_terrain_hlsl.py, which pastes them into its helper struct (FTemporaryTerrain Look):

- TexelBlend: a blend texel of 4 exact floats (Bridge/tile_blend_atlas.hpp: blendTexelFloats), the
  same for the atlas of the tiles and the blend textures of the horizon
  (Bridge/horizon_blend_texels.hpp), decoded to its 4 fields;
- AddBlend: adds one decoded blend texel (BlendTexel: 3 biome and 3 material ids, their weights)
  with its share, never interpolating its ids (biome_blend.hpp: addTexel);
- FinishBlend: the layers of the summed weights, with the overrides of the materials;
- BlendLayers: the 4 texels of a tile in the atlas (Bridge/tile_blend_atlas.hpp) around a point;
- BlendTexelsLayers: up to 4 texels of the horizon with their shares (the morph of the horizon,
  the bilinear texels of level 0 under the tiles).

The weights of the biomes and the materials are summed in float4 lanes (wb0: biomes 0-3, wb1:
4-7...; wm0: materials 0-3...), as many as the counts below need: the code is generated from them
(agent 49), so that a new biome or material only adds lanes, never an array indexed at run time.
BIOME_A, BIOME_B, BIOME_C and TEXELS are filled by temporary_terrain_hlsl.struct_hlsl, NBIOMES and
LASTBIOME by fill (its tables of the biomes). No identifier may be a type Unreal rewrites (half,
fixed...) or a reserved word of HLSL (sample, point, linear...): unreal_horizon_blend_scripts_test
checks it, and every name declared here against those of the struct and of the custom nodes it is
pasted into.
"""

# The ids of the core (terrain_core biome.hpp: kBiomeCount, surface_material.hpp:
# kSurfaceMaterialCount) and the fields of a blend texel (biome_blend.hpp: kBiomeIdBits,
# kMaterialIdBits, kBlendPresentBit, kBlendDuneShift). unreal_horizon_blend_scripts_test reads
# them in the headers of the core and checks that they match.
BIOME_COUNT = 12
MATERIAL_COUNT = 8
BIOME_ID_BITS = 6
MATERIAL_ID_BITS = 5
PRESENT_BIT = 23
DUNE_SHIFT = MATERIAL_ID_BITS * 3
# The packed surface of a texel of the horizon (Bridge/terrain_surface.hpp): material +
# SURFACE_BIOME_STRIDE x biome, + SURFACE_WATER_CODE under a lake or a river.
SURFACE_BIOME_STRIDE = 1 << MATERIAL_ID_BITS
SURFACE_WATER_CODE = 1 << (MATERIAL_ID_BITS + BIOME_ID_BITS)


def lanes(count):
    """The float4 lanes that hold count weights."""
    return (count + 3) // 4


def lane(prefix, index):
    """The lane of one id: wb0.x for biome 0, wm1.w for material 7."""
    return f"{prefix}{index // 4}.{'xyzw'[index % 4]}"


def _names(prefix, count):
    return [f"{prefix}{k}" for k in range(lanes(count))]


B_LANES = _names("wb", BIOME_COUNT)
M_LANES = _names("wm", MATERIAL_COUNT)
ALL_LANES = B_LANES + M_LANES
# The lanes as parameters of a function, as arguments of a call, and declared to 0.
LANE_PARAMS = ", ".join(f"inout float4 {name}" for name in ALL_LANES)
LANE_INPUTS = ", ".join(f"float4 {name}" for name in ALL_LANES)
LANE_ARGS = ", ".join(ALL_LANES)
LANE_ZEROS = (f"float4 {ALL_LANES[0]} = float4(0.0, 0.0, 0.0, 0.0);\n        float4 "
              + ", ".join(f"{name} = {ALL_LANES[0]}" for name in ALL_LANES[1:]) + ";")


def _add_lanes(prefix, count, weight, idname):
    return "\n".join(f"            {name} += {weight} * (float4)(lanes + {4.0 * k:.1f} == {idname});"
                     for k, name in enumerate(_names(prefix, count)))


def _weights_list():
    return ", ".join(lane("wb", n) for n in range(BIOME_COUNT))


def fill(text):
    """The counts of the biomes in text: NBIOMES and LASTBIOME (the tables of the struct)."""
    return text.replace("NBIOMES", str(BIOME_COUNT)).replace("LASTBIOME", str(BIOME_COUNT - 1))


_ADD = r"""
    // A blend texel of 4 exact floats (Bridge/tile_blend_atlas.hpp: the fields biomes, materials,
    // biomeWeights and materialWeights of biome_blend.hpp, each an integer below 2^24), read
    // without filtering: its fields in v, each float an exact integer, converted without rounding
    // (from 2^23 on, t + 0.5 rounds an odd integer up: the present bit makes the biomes that
    // large); false where it holds no blend (kBlendPresentBit of its biomes unset: a texel never
    // written, or the texels of no tile).
    bool TexelBlend(float4 t, out uint4 v)
    {
        v = (uint4)max(t, 0.0);
        return (v.x & PRESENTu) != 0u;
    }

    // Adds one blend texel, its share times its weights (biome_blend.hpp: addTexel), never
    // interpolating its ids: the weight of each biome (wb0: biomes 0-3, wb1: 4-7...) and of each
    // material (wm0: materials 0-3...), added through masks rather than indexed arrays (no array
    // indexed at run time: no scratch memory on the GPU). v: the fields of the texel (TexelBlend;
    // the share of the dunes of an erg, agent 34, in 127ths from kBlendDuneShift of its materials);
    // wd: the shares times the share of the dunes.
    void AddBlend(uint4 v, float share, LANE_PARAMS, inout float wd)
    {
        wd += share * (float)((v.y >> DUNEu) & 127u) / 127.0;
        float3 wb = float3(v.z & 255u, (v.z >> 8u) & 255u, 0.0) / 255.0;
        wb.z = max(1.0 - wb.x - wb.y, 0.0);
        float3 wt = float3(v.w & 255u, (v.w >> 8u) & 255u, 0.0) / 255.0;
        wt.z = max(1.0 - wt.x - wt.y, 0.0);
        const float4 lanes = float4(0.0, 1.0, 2.0, 3.0);
        [unroll] for (int k = 0; k < 3; ++k)
        {
            float biome = (float)min((v.x >> (BIOME_BITSu * k)) & BIOME_MASKu, BIOME_LASTu);
            float material = (float)min((v.y >> (MATERIAL_BITSu * k)) & MATERIAL_MASKu, MATERIAL_LASTu);
            float w = share * wb[k];
            float wmk = share * wt[k];
BIOME_ADDS
MATERIAL_ADDS
        }
    }
"""

_FINISH = r"""
    // The layers of the summed blends (AddBlend): those of the biomes (BIOMES), with the
    // overrides of the materials (as in Layers), both linear in the weights; the weight of the
    // ocean and of the desert tell the river bed and the sand apart; the sand of the dunes is the
    // smooth sand, never the stony desert around it. wd, d: the share of the dunes of an erg
    // (biome_blend.hpp: its floor below 16 127ths, its dunes above).
    void FinishBlend(LANE_INPUTS, float wd, out float4 a, out float4 b, out float2 c, out float d)
    {
        d = saturate(wd);
        float dune = saturate((d - 16.0 / 127.0) / (1.0 - 16.0 / 127.0));
        const float4 BA[NBIOMES] = { BIOME_A };
        const float4 BB[NBIOMES] = { BIOME_B };
        const float2 BC[NBIOMES] = { BIOME_C };
        float wbs[NBIOMES] = { WEIGHTS_LIST };
        float4 A = float4(0.0, 0.0, 0.0, 0.0);
        float4 B = A;
        c = float2(0.0, 0.0);
        [unroll] for (int n = 0; n < NBIOMES; ++n)
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
"""

_READERS = r"""
    // The layers of the surface blend of a tile (Bridge/tile_blend_atlas.hpp): the 4 texels of
    // its slot of the atlas T around the point at local (centimeters from the lowest corner of the
    // tile), pitchCm apart, each adding its bilinear share (AddBlend), then FinishBlend (d: the
    // share of the dunes). False where the tile has no texels (slot -1, or the texels of no tile).
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
        LANE_ZEROS
        float wd = 0.0;
        [unroll] for (int t = 0; t < 4; ++t)
        {
            uint4 v;
            if (!TexelBlend(T.Load(int3(corner + int2(base) + int2(t & 1, t >> 1), 0)), v)) { return false; }
            AddBlend(v, share[t], LANE_ARGS, wd);
        }
        FinishBlend(LANE_ARGS, wd, a, b, c, d);
        return true;
    }

    // The layers of up to 4 blend texels of the horizon, each with its share (the shares sum to
    // 1): every texel decoded first, its weights added with its share (AddBlend), never its ids
    // interpolated, then FinishBlend (d: the share of the dunes). False, the layers and d
    // untouched, unless every texel of a share above 0 holds a blend.
    bool BlendTexelsLayers(float4 t0, float4 t1, float4 t2, float4 t3, float4 share,
                           inout float4 a, inout float4 b, inout float2 c, inout float d)
    {
        uint4 v0, v1, v2, v3;
        bool present = TexelBlend(t0, v0) || share.x <= 0.0;
        present = (TexelBlend(t1, v1) || share.y <= 0.0) && present;
        present = (TexelBlend(t2, v2) || share.z <= 0.0) && present;
        present = (TexelBlend(t3, v3) || share.w <= 0.0) && present;
        if (!present) { return false; }
        LANE_ZEROS
        float wd = 0.0;
        // The texels of no share (the horizon away from the border of its window) are skipped.
        BRANCH if (share.x > 0.0) { AddBlend(v0, share.x, LANE_ARGS, wd); }
        BRANCH if (share.y > 0.0) { AddBlend(v1, share.y, LANE_ARGS, wd); }
        BRANCH if (share.z > 0.0) { AddBlend(v2, share.z, LANE_ARGS, wd); }
        BRANCH if (share.w > 0.0) { AddBlend(v3, share.w, LANE_ARGS, wd); }
        FinishBlend(LANE_ARGS, wd, a, b, c, d);
        return true;
    }
"""


def _generate():
    code = (_ADD + _FINISH + _READERS)
    code = code.replace("BIOME_ADDS", _add_lanes("wb", BIOME_COUNT, "w", "biome"))
    code = code.replace("MATERIAL_ADDS", _add_lanes("wm", MATERIAL_COUNT, "wmk", "material"))
    code = code.replace("LANE_PARAMS", LANE_PARAMS).replace("LANE_INPUTS", LANE_INPUTS)
    code = code.replace("LANE_ARGS", LANE_ARGS)
    code = code.replace("LANE_ZEROS", LANE_ZEROS).replace("WEIGHTS_LIST", _weights_list())
    numbers = {"PRESENT": 1 << PRESENT_BIT, "DUNE": DUNE_SHIFT, "BIOME_BITS": BIOME_ID_BITS,
               "BIOME_MASK": (1 << BIOME_ID_BITS) - 1, "BIOME_LAST": BIOME_COUNT - 1,
               "MATERIAL_BITS": MATERIAL_ID_BITS, "MATERIAL_MASK": (1 << MATERIAL_ID_BITS) - 1,
               "MATERIAL_LAST": MATERIAL_COUNT - 1}
    for name in sorted(numbers, key=len, reverse=True):
        code = code.replace(f"{name}u", f"{numbers[name]}u")
    return fill(code) + "\n"


BLEND_HLSL = _generate()
