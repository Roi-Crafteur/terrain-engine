"""The look of the lakes and rivers drawn at their level (agent 27): one HLSL for the tiles
(create_water_materials.py, M_TerrainWater) and for the far horizon (horizon_water_hlsl.py), with
the same parameters, so that the water of the tiles meets the water of the horizon without a
change of look.

Custom nodes cannot declare functions: the helpers live in a struct (FTerrainWater Water).
- Body: the color of deep water and its absorption (per meter, red green blue) by its kind and its
  place: a river brown-green (its silt), a lake green, a mountain lake turquoise (glacial flour,
  from GlacierFromM to GlacierFullM of altitude), a salt lake pale turquoise. Through a thickness
  of water the bed fades by exp(-absorption * thickness) into that color (Beer-Lambert): the
  shallows show their bed, deep water its color.
- Waves: ripples, sine waves fixed in the world (whole cycles per 4,096 m, the period of the waves
  of the sea and of the coordinates of the world, so that nothing moves after a turn of the world),
  each fading into the roughness when a pixel covers less than 2 of its wavelengths.
- Ripples: the waves carried downstream by the flow of a river in two phases half a cycle apart,
  faded in and out (the pattern never pulses nor stretches); still on a lake.
- Noise: value noise for the foam of the rapids.
The reflection of the sky (Fresnel, from the sky view LUT of the SkyAtmosphere, as the sea) and the
glint of the sun (GGX) are written in the nodes (SKY_HLSL): they need the parameters of the pixel.
No texture: nothing to license.
"""

# The look, tunable on the instances. Colors of deep water (linear) and absorptions (1/m).
WATER_COLORS = {
    "RiverWaterColor": (0.050, 0.044, 0.022), "LakeWaterColor": (0.014, 0.046, 0.040),
    "GlacierWaterColor": (0.010, 0.120, 0.125), "SaltWaterColor": (0.060, 0.170, 0.160),
    "RiverAbsorption": (0.40, 0.45, 0.62), "LakeAbsorption": (0.42, 0.13, 0.17),
    "GlacierAbsorption": (0.46, 0.075, 0.065), "SaltAbsorption": (0.32, 0.065, 0.055),
    "FoamColor": (0.80, 0.85, 0.86), "CurtainColor": (0.55, 0.66, 0.70),
}
# Altitudes of the turquoise of the mountain lakes, the strength of the ripples, the roughness of
# the water between them (GGX alpha), the speed of the flow of the rivers (m/s), the slopes of the
# water where the foam of the rapids starts and is full.
WATER_SCALARS = {
    "GlacierFromM": 1400.0, "GlacierFullM": 2600.0, "RippleStrength": 1.0, "WaterAlpha": 0.03,
    "FlowSpeed": 1.2, "FoamFromSlope": 0.04, "FoamFullSlope": 0.20,
}

# Ripples: (a, b) whole cycles per 4,096 m along X and Y, slope amplitude, phase; from 5.6 m to
# 0.35 m, spread around the wind, no two alike.
RIPPLES = [(730, 103, 0.05, 0.3), (-311, 1187, 0.05, 2.1), (1693, 571, 0.055, 4.4),
           (2390, -1406, 0.055, 1.2), (607, 3611, 0.05, 5.0), (4997, 1290, 0.045, 3.3),
           (-3305, 5866, 0.04, 0.8), (9871, 6207, 0.035, 2.6)]

STRUCT_HLSL = r"""
struct FTerrainWater
{
    // The deep color and the absorption of water of a kind (1 river, 2 lake, 3 salt lake) at
    // altitude (meters).
    void Body(float kind, float altitude, float3 river, float3 lake, float3 glacier, float3 salt,
              float3 riverK, float3 lakeK, float3 glacierK, float3 saltK, float glacierFrom,
              float glacierFull, out float3 color, out float3 absorb)
    {
        float g = saturate((altitude - glacierFrom) / max(glacierFull - glacierFrom, 1.0));
        bool isRiver = kind < 1.5;
        g *= isRiver ? 0.5 : 1.0;
        color = lerp(isRiver ? river : lake, glacier, g);
        absorb = lerp(isRiver ? riverK : lakeK, glacierK, g);
        if (kind > 2.5) { color = salt; absorb = saltK; }
    }

    // The slopes of the ripples at p (meters, modulo 4096 m), at time (s), a pixel covering
    // footprint meters; filtered: the variance of the slopes faded into the roughness.
    float2 Waves(float2 p, float time, float footprint, out float filtered)
    {
        const float PI2 = 6.28318531;
        float2 uv = frac(p / 4096.0);
        float2 slopes = float2(0.0, 0.0);
        filtered = 0.0;
RIPPLE_LOOP
        return slopes;
    }

    // The ripples carried downstream by flow (m/s): two phases half a cycle of 4 s apart.
    float2 Ripples(float2 p, float2 flow, float time, float footprint, out float filtered)
    {
        BRANCH
        if (dot(flow, flow) < 1e-6)
        {
            return Waves(p, time, footprint, filtered);
        }
        float t0 = frac(time / 4.0);
        float t1 = frac(time / 4.0 + 0.5);
        float f0, f1;
        float2 s0 = Waves(p - flow * (t0 * 4.0), time, footprint, f0);
        float2 s1 = Waves(p - flow * (t1 * 4.0) + 37.0, time, footprint, f1);
        float w0 = 1.0 - abs(2.0 * t0 - 1.0);
        filtered = w0 * f0 + (1.0 - w0) * f1;
        return w0 * s0 + (1.0 - w0) * s1;
    }

    // A hash of a cell in [0, 1) without sine (precise for large cells on every GPU).
    float Hash(float2 c)
    {
        float3 h = frac(float3(c.xyx) * 0.1031);
        h += dot(h, h.yzx + 33.33);
        return frac((h.x + h.y) * h.z);
    }

    // Value noise of cells of size meters at p, in [0, 1].
    float Noise(float2 p, float size)
    {
        float2 q = p / size;
        float2 i = floor(q);
        float2 f = q - i;
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(Hash(i), Hash(i + float2(1.0, 0.0)), f.x),
                    lerp(Hash(i + float2(0.0, 1.0)), Hash(i + float2(1.0, 1.0)), f.x), f.y);
    }
};
FTerrainWater Water;
"""

RIPPLE_TEMPLATE = (
    "        {{ float2 kv = float2({a}.0, {b}.0); float lambda = 4096.0 / length(kv);\n"
    "          float omega = sqrt(9.81 * PI2 / lambda + 0.074 * PI2 * PI2 * PI2 / (1000.0 * lambda * lambda * lambda));\n"
    "          float phase = PI2 * frac(dot(kv, uv)) - omega * time + {phase};\n"
    "          float keep = saturate(log2(lambda / (2.0 * footprint)) * 0.333);\n"
    "          slopes += keep * {slope} * cos(phase) * normalize(kv);\n"
    "          filtered += (1.0 - keep * keep) * {slope} * {slope}; }}\n")

# The sky in the reflected direction and the glint of the sun, for a pixel of normal n seen along
# V (toward the camera), the roughness alpha2 (GGX alpha squared): reflection (radiance), Glint
# (radiance, capped), Fresnel. Needs SunDir, SunIlluminance and the sky view LUT bound.
SKY_HLSL = r"""
    float a = sqrt(alpha2);
    float NoV = saturate(dot(n, V)) + 1e-4;
    Fresnel = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);
    float3 r = reflect(-V, n);
    r = normalize(float3(r.xy, max(r.z, 0.003 + 0.15 * a)));
    reflection = Fresnel * MaterialExpressionSkyAtmosphereViewLuminance(Parameters, r);
    float3 L = normalize(SunDir);
    float3 H = normalize(L + V);
    float NoL = saturate(dot(n, L));
    float NoH = saturate(dot(n, H));
    float VoH = saturate(dot(V, H));
    float dd = (NoH * alpha2 - NoH) * NoH + 1.0;
    float D = alpha2 / (3.14159265 * dd * dd);
    float Vis = 0.5 / (NoL * (NoV * (1.0 - a) + a) + NoV * (NoL * (1.0 - a) + a) + 1e-5);
    float Fs = 0.02 + 0.98 * pow(1.0 - VoH, 5.0);
    Glint = min(SunIlluminance * (NoL * D * Vis * Fs), SunIlluminance * 64.0);
"""

# The body of a pixel of water: Body of its kind and place (inputs Kind, Altitude), the look
# parameters by name.
BODY_CALL = ("Water.Body(Kind, Altitude, RiverWaterColor, LakeWaterColor, GlacierWaterColor, "
             "SaltWaterColor, RiverAbsorption, LakeAbsorption, GlacierAbsorption, SaltAbsorption, "
             "GlacierFromM, GlacierFullM, deep, absorb);")


# The pixel of the surface of a lake or a river of a tile (M_TerrainWater, translucent): inputs Flow
# (UV0), KindSlope (UV1), Altitude (UV2.x), CameraRel, Time, SunDir, SunIlluminance, SkyView (binds
# the sky view LUT), SceneDepth and PixelDepth (cm), VertexNormal, the look parameters; returns the
# base color, with EmissiveOut, OpacityOut, NormalOut (world space) and RoughnessOut. The color the
# water lets through is one share for the three channels: the same as the horizon draws it.
TILE_WATER_HLSL = r"""
const float PERIOD = 409600.0; // 4096 m, cm
float2 p = (GetTranslatedWorldPosition(Parameters).xy - GetPeriodicWorldOrigin(float3(PERIOD, PERIOD, PERIOD)).xy) * 0.01;
float footprint = max(length(ddx(p)), length(ddy(p))) + 1e-3; // meters per pixel
float Kind = KindSlope.x;
float slope = KindSlope.y;
float2 flow = Flow * FlowSpeed;
float filtered;
float2 ripples = Water.Ripples(p, flow, Time, footprint, filtered) * RippleStrength;
float alpha2 = WaterAlpha * WaterAlpha + filtered * RippleStrength * RippleStrength + 0.05 * slope;
float3 V = normalize(-CameraRel);
float3 n = normalize(normalize(VertexNormal) + float3(-ripples, 0.0));
float Fresnel;
float3 reflection;
float3 Glint;
SKY_CODE
// The water between the surface and what lies behind it (the bed, a bank, a block), meters.
float thickness = max(SceneDepth - PixelDepth, 0.0) * 0.01;
float3 deep, absorb;
BODY_CODE
float through = dot(exp(-absorb * thickness), float3(0.3333, 0.3333, 0.3334));
// The foam of the rapids, where the water slopes, carried by the flow.
float2 q = p - flow * Time;
float foam = saturate((slope - FoamFromSlope) / max(FoamFullSlope - FoamFromSlope, 1e-3));
foam *= smoothstep(0.35, 0.75, 0.6 * Water.Noise(q, 0.7) + 0.4 * Water.Noise(q * 1.3 + 11.0, 2.3));
// Translucent: what the water keeps of the light behind it, the foam opaque.
float opacity = max(saturate(1.0 - through * (1.0 - Fresnel)), foam);
float keep = max(opacity, 0.05);
EmissiveOut = (reflection + Glint) * (1.0 - foam) / keep;
OpacityOut = opacity;
NormalOut = n;
RoughnessOut = lerp(1.0, 0.8, foam);
return lerp(deep * (1.0 - through) * (1.0 - Fresnel) / keep, FoamColor, foam);
"""

# The pixel of the curtain of a fall or of the foam at its foot (M_TerrainFalls, translucent, two
# sided): inputs Fall (UV0: across and down the fall in meters; on the foam, meters from its
# center), KindDrop (UV1), AltitudeSize (UV2: altitude, width of the curtain or radius of the
# foam), Time, SceneDepth, PixelDepth, CurtainColor, FoamColor; returns the base color, with
# OpacityOut.
FALLS_HLSL = r"""
float size = max(AltitudeSize.y, 0.1);
// Soft where it meets the rock or the water behind it.
float soft = saturate((SceneDepth - PixelDepth) * 0.01 / 1.5);
float opacity;
float3 color;
BRANCH
if (KindDrop.x < 4.5)
{
    // Streaks falling with the water, faster and faster: they move by the time it takes to fall
    // that far, the pattern never stretching.
    float y = (sqrt(2.0 * max(Fall.y, 0.0) / 9.81) - Time) * 3.0;
    float u = Fall.x;
    float s = 0.55 * Water.Noise(float2(u * 1.3, y), 1.0)
            + 0.30 * Water.Noise(float2(u * 3.1, y * 2.0) + 17.0, 1.0)
            + 0.15 * Water.Noise(float2(u * 0.4, y * 0.5) + 5.0, 1.0);
    float edge = saturate(min(u, size - u) / 1.5);
    float top = saturate(Fall.y + 0.2);
    opacity = saturate(0.35 + 0.6 * s) * edge * top;
    color = lerp(CurtainColor, FoamColor, saturate(0.8 * s + 0.3 * Fall.y / max(KindDrop.y, 1.0)));
}
else
{
    // The foam at the foot, churning, thinner away from the curtain.
    float r = length(Fall) / size;
    float n = 0.6 * Water.Noise(Fall + float2(Time * 0.7, -Time * 0.4), 0.9)
            + 0.4 * Water.Noise(Fall * 1.7 - float2(Time * 0.5, Time * 0.9), 0.9);
    opacity = saturate(1.0 - r) * smoothstep(0.25, 0.7, n + 0.3 * (1.0 - r));
    color = FoamColor;
}
OpacityOut = opacity * soft;
return color;
"""


def tile_water_hlsl():
    """The code of the pixel of the water of the tiles."""
    return (TILE_WATER_HLSL.replace("SKY_CODE", SKY_HLSL)
            .replace("BODY_CODE", BODY_CALL))


def struct_hlsl():
    """The helper struct, its ripples unrolled."""
    loop = "".join(RIPPLE_TEMPLATE.format(a=a, b=b, slope=slope, phase=phase)
                   for a, b, slope, phase in RIPPLES)
    return STRUCT_HLSL.replace("RIPPLE_LOOP", loop)


def look_inputs():
    """The inputs of a node that colors the water: the look parameters, by name."""
    return list(WATER_COLORS) + list(WATER_SCALARS)
