"""The HLSL of the sea of the GPU horizon, for create_horizon_gpu_material.py.

- sea_hlsl(): the sea of a pixel (SEA_HLSL, its waves unrolled from WAVES): its mask, normal,
  body color, glint and reflection.
- SQUARE_HLSL: the opacity mask. The sea square under the tiles is drawn only where the core puts
  the ocean: the bilinear share of the sea texels of level 0 (t.a < 0, Bridge/horizon_textures.hpp)
  at least one half. A dry depression below the sea level shows its ground, not a sea at 0 m.
- BEHIND_HLSL: the pixel depth offset. Where the tiles may cover the land of level 0, its pixels
  lie deeper by BehindTilesCm, so that the tiles stay in front where the two surfaces cross (the
  8 m cells of level 0 against the meshes of the tiles): no checkerboard of horizon cells beyond
  the band where the drop fades out. Not on the sea, which stays in front of the shallow sea floor.
"""

# The ground and the sand at the sea level: altitudeColor(kSeaLevel) (Bridge/altitude_color.cpp).
SHORE = "float3(194.0, 179.0, 128.0) / 255.0"

# Waves: (a, b) whole cycles per period of 4096 m along X and Y (kSeaWavePeriodMeters,
# Bridge/horizon_atmosphere.hpp), slope amplitude, phase. From 90 m to 1.6 m, spread around the
# wind (30 degrees), no two alike: no pattern repeats. Each fades out between 16 and 2 pixels
# per wavelength, its slopes going into the roughness of the glint.
WAVES = [(39, 23, 0.035, 0.0), (61, -5, 0.035, 1.3), (21, 79, 0.04, 2.9), (109, 19, 0.045, 4.1),
         (87, 125, 0.05, 0.7), (178, -103, 0.05, 5.3), (216, 181, 0.05, 3.6),
         (-34, 385, 0.05, 2.2), (500, 182, 0.05, 0.4), (706, -189, 0.045, 4.8),
         (422, 905, 0.045, 1.9), (1360, 119, 0.04, 3.1), (1317, 1317, 0.04, 5.9),
         (1961, -1646, 0.035, 2.6)]

SEA_HLSL = r"""
const float PI2 = 6.28318531;
const float PERIOD = 409600.0; // kSeaWavePeriodMeters, cm
float ground = Ground;
float coast = fwidth(Ground);
// Waves fixed in the world: the position modulo the period, from the origin of the core; its
// derivatives, the size of a pixel, outside of any branch.
float2 w = GetTranslatedWorldPosition(Parameters).xy - GetPeriodicWorldOrigin(float3(PERIOD, PERIOD, PERIOD)).xy - float2(WaveOriginX, WaveOriginY);
float footprint = max(length(ddx(w)), length(ddy(w))) * 0.01 + 1e-3; // meters per pixel
BRANCH
if (Kind.g > 0.5)
{
    // The sea square under the tiles: its ground from the texels of level 0, bilinear.
    int side = (int)Cells + 1;
    int2 first = int2((int)FirstSlotX, (int)FirstSlotZ);
    float2 p = clamp(LocalPos.xy, 0.0, Cells - 0.001);
    int2 i0 = int2(floor(p));
    float2 f = p - float2(i0);
    float4 t00 = Tex.Load(int3((i0 + first) % side, 0));
    float4 t10 = Tex.Load(int3((i0 + int2(1, 0) + first) % side, 0));
    float4 t01 = Tex.Load(int3((i0 + int2(0, 1) + first) % side, 0));
    float4 t11 = Tex.Load(int3((i0 + int2(1, 1) + first) % side, 0));
    float4 g = float4(t00.a < 0.0 ? t00.a : max(t00.r, 0.0), t10.a < 0.0 ? t10.a : max(t10.r, 0.0),
                      t01.a < 0.0 ? t01.a : max(t01.r, 0.0), t11.a < 0.0 ? t11.a : max(t11.r, 0.0));
    ground = min(lerp(lerp(g.x, g.y, f.x), lerp(g.z, g.w, f.x), f.y), -0.05);
}
// The sea where the ground lies below the sea level, antialiased across the coast line.
SeaMask = Kind.g > 0.5 ? 1.0 : saturate(0.5 - ground / max(coast, 1e-4));
SeaBody = float3(0.0, 0.0, 0.0);
SeaNormal = float3(0.0, 0.0, 1.0);
Glint = float3(0.0, 0.0, 0.0);
float3 reflection = float3(0.0, 0.0, 0.0);
BRANCH
if (SeaMask > 0.0)
{
    float depth = max(-ground, 0.0);
    float2 uv = frac(w / PERIOD);
    float2 slopes = float2(0.0, 0.0);
    float filtered = 0.0;
WAVE_LOOP
    float alpha2 = SeaAlpha * SeaAlpha + filtered * SeaWaveScale * SeaWaveScale;
    float a = sqrt(alpha2);
    float3 V = normalize(-CameraRel);
    float3 n = normalize(float3(-slopes * SeaWaveScale + CameraRel.xy / PlanetRadiusCm, 1.0));
    float NoV = saturate(dot(n, V)) + 1e-4;
    float Fresnel = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);
    // The sky in the reflected direction, from the sky view LUT of the SkyAtmosphere (the sky
    // seen from the camera, sharp at the horizon; the blurred cubemap of the sky light would
    // darken the horizon), its lobe kept above the horizon by part of its width.
    float3 r = reflect(-V, n);
    r = normalize(float3(r.xy, max(r.z, 0.003 + 0.15 * a)));
    reflection = Fresnel * MaterialExpressionSkyAtmosphereViewLuminance(Parameters, r);
    // The water by its depth: the sand under the shallows, turquoise, then deep blue.
    float3 body = lerp(SeaDeepColor, SeaOpenColor, exp(-depth / 60.0));
    body = lerp(body, SeaShallowColor, exp(-depth / 6.0));
    body = lerp(body, SHORE_COLOR * float3(0.55, 0.75, 0.72), exp(-depth / 1.2));
    SeaBody = body * (1.0 - Fresnel);
    SeaNormal = n;
    // The glint of the sun (GGX, Smith), its light after the atmosphere at this point.
    float3 L = normalize(SunDir);
    float3 H = normalize(L + V);
    float NoL = saturate(dot(n, L));
    float NoH = saturate(dot(n, H));
    float VoH = saturate(dot(V, H));
    float d = (NoH * alpha2 - NoH) * NoH + 1.0;
    float D = alpha2 / (3.14159265 * d * d);
    float Vis = 0.5 / (NoL * (NoV * (1.0 - a) + a) + NoV * (NoL * (1.0 - a) + a) + 1e-5);
    float Fs = 0.02 + 0.98 * pow(1.0 - VoH, 5.0);
    Glint = min(SunIlluminance * (NoL * D * Vis * Fs), SunIlluminance * 64.0);
}
return reflection + 0.0 * SkyView;
""".replace("SHORE_COLOR", SHORE)

WAVE_TEMPLATE = (
    "{{ float2 kv = float2({a}.0, {b}.0); float lambda = 4096.0 / length(kv);\n"
    "  float omega = sqrt(9.81 * PI2 / lambda);\n"
    "  float phase = PI2 * frac(dot(kv, uv)) - omega * Time + {phase};\n"
    "  float keep = saturate(log2(lambda / (2.0 * footprint)) * 0.333);\n"
    "  slopes += keep * {slope} * cos(phase) * normalize(kv);\n"
    "  filtered += (1.0 - keep * keep) * {slope} * {slope}; }}\n")

SQUARE_HLSL = r"""
if (Kind.g < 0.5) { return 1.0; }
int side = (int)Cells + 1;
int2 first = int2((int)FirstSlotX, (int)FirstSlotZ);
float2 p = clamp(LocalPos.xy, 0.0, Cells - 0.001);
int2 i0 = int2(floor(p));
float2 f = p - float2(i0);
float4 s = float4(Tex.Load(int3((i0 + first) % side, 0)).a < 0.0,
                  Tex.Load(int3((i0 + int2(1, 0) + first) % side, 0)).a < 0.0,
                  Tex.Load(int3((i0 + int2(0, 1) + first) % side, 0)).a < 0.0,
                  Tex.Load(int3((i0 + int2(1, 1) + first) % side, 0)).a < 0.0);
return step(0.5, lerp(lerp(s.x, s.y, f.x), lerp(s.z, s.w, f.x), f.y));
"""

BEHIND_HLSL = r"""
if (Kind.g > 0.5) { return 0.0; }
// Full up to BehindBandCm beyond the band the tiles always cover (they reach about one tile of
// LOD 3 further), then down to 0 across BehindFadeCm, and from the sea level to 1 m of ground:
// no step in the depth, which the screen space effects would draw as a dark line.
float fromCenter = max(abs(LocalPos.x - Cells * 0.5), abs(LocalPos.y - Cells * 0.5)) * PitchCm;
float reach = DropCoveredCm + BehindBandCm + BehindFadeCm;
return BehindTilesCm * saturate((reach - fromCenter) / max(BehindFadeCm, 1.0)) * saturate(Ground);
"""


def sea_hlsl():
    """The code of the sea of a pixel, its waves unrolled."""
    loop = "".join(WAVE_TEMPLATE.format(a=a, b=b, slope=slope, phase=phase)
                   for a, b, slope, phase in WAVES)
    return SEA_HLSL.replace("WAVE_LOOP", loop)
