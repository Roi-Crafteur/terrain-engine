"""The far detail of the layers and the warmth of the rock (agent 40), for the shading of the
ground of the tiles and of the far horizon (temporary_terrain_hlsl.py: shade_hlsl puts them in
place of FAR_BAND, FAR_LAYER, FAR_ROCK and ROCK_WARMTH). Each behind its switch, 1 by default
(temporary_terrain_look.py: SCALARS), 0 the look before, to the bit.

- FarDetail: beyond the tiles the layers showed only the mean color of their textures (one flat
  tone per layer from 400 m on). One more look at the texture of each layer, at two large scales
  (13 and 31 times its tiling, the second turned: neither period shows), the mips taken from the
  derivatives of the pixel, comes in from 300 m, where the detail of the tiles fades (DetailNearM
  150 to DetailFarM 400 m), and goes out by 4 km. The rock takes only its light and dark (its
  tints kept).
- RockWarmth: the rock a little warmer. Lit by the blue sky alone in the shade, it read too blue
  (blue over red 1.67 in Unreal); RockGrey could not help, the blue being the light's, not the
  rock's. Never as orange as the sand: blue over red of the factor 0.8.
"""

# Before the shading code: the helper of the far detail (a struct: a Custom node takes no
# function).
FAR_STRUCT = r"""
struct FFarDetail
{
    // The texture of a layer at two large scales, the second turned, averaged.
    float3 Of(Texture2D T, SamplerState S, float2 p, float2 dx, float2 dy, float scale)
    {
        const float2x2 turn = float2x2(0.8, -0.6, 0.6, 0.8);
        float a = 1.0 / (scale * 13.0);
        float b = 1.0 / (scale * 31.0);
        float3 first = T.SampleGrad(S, p * a, dx * a, dy * a).rgb;
        float3 second = T.SampleGrad(S, mul(turn, p) * b, mul(turn, dx) * b, mul(turn, dy) * b).rgb;
        return 0.5 * (first + second);
    }
};
FFarDetail Far;
"""

# Where the far detail shows (after the detail of the tiles, Dist in meters).
FAR_BAND = r"""// The far detail (agent 40, far_detail_hlsl.py): in from 300 m, out by 4 km.
float far = FarDetail * saturate((Dist - 300.0) / 100.0)
          * (1.0 - saturate((Dist - 3000.0) / 1000.0));"""

# In each ground layer, after its detail (c: its color so far).
FAR_LAYER = r"""    BRANCH if (far > 0.0)
    {
        c = lerp(c, Far.Of(TAI, TAISampler, P, dPx.xy, dPy.xy, SCALE), far);
    }
"""

# The rock: the light and dark of its texture at large scales (TAR: its texture).
FAR_ROCK = r"""BRANCH if (far > 0.0)
{
    float3 luma = float3(0.3, 0.59, 0.11);
    float meanLum = dot(TAR.SampleLevel(TARSampler, P / SCALE, 13.0).rgb, luma);
    float farLum = dot(Far.Of(TAR, TARSampler, P, dPx.xy, dPy.xy, SCALE), luma);
    rockC *= lerp(1.0, farLum / max(meanLum, 1e-3), far);
}
"""

# After the color of the rock (rockG).
ROCK_WARMTH = r"""// The rock a little warmer (agent 40, far_detail_hlsl.py).
rockG *= lerp(float3(1.0, 1.0, 1.0), float3(1.0, 0.94, 0.8), RockWarmth);"""
