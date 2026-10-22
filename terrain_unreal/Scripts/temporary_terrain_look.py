"""The TEMPORARY terrain material of phase 14: a readable stand-in until the real materials.

Shared by create_temporary_terrain_material.py (the tiles, /Game/TerrainEngine/M_TemporaryTerrain)
and create_horizon_gpu_material.py (the far horizon), so that the near and far ground take their
layers by the same rules. To replace it, swap this module and the tile material; nothing in the
C++ depends on the layers.

Rules (kept simple on purpose):
- the material and the biome of the core (packed in the texels of the horizon,
  Bridge/terrain_surface.hpp) pick the ground layers: grass, dry grass, forest floor, alpine
  grass, dirt, sand, desert, scree, with soft transitions (blended per vertex between samples);
- rock from about RockStartDeg of slope, fully rock from RockFullDeg, the threshold jittered by
  noise, and wherever the core puts rock (CoreRock of it, at every distance);
- snow where the core says it is cold (snow biome or material, which includes the altitude and the
  exposure), only on slopes gentler than SnowShedDeg: rock pokes through on the steep faces;
- scree at the foot of steep slopes (tiles: the steepest of the 4 texels around a vertex);
- world-aligned textures (top projection for the ground, triplanar for the rock), faded beyond
  DetailNearM..DetailFarM to the mean color of each texture (its last mip), which is all the
  horizon uses: near tiles, far tiles and the horizon meet without a seam;
- macro variation by noise against tiling, then saturation and warmth.

Textures: the Stylescape HD pack (Fab license: never in the git repository) under
/Game/StylescapeHD/Landscape/Textures in the game project. Without it, every layer falls back to a
flat color (FALLBACK below): the scripts still build working materials.
"""

import unreal

STYLESCAPE = "/Game/StylescapeHD/Landscape/Textures/"
WHITE = "/Engine/EngineResources/WhiteSquareTexture"
FLAT_NORMAL = "/Engine/EngineMaterials/FlatNormal"

# (name, albedo, normal, fallback linear color = the mean of the albedo, meters per repeat)
# Repeats divide 4096 m, the period of the world-aligned coordinates (they also divide each
# side of the wrapped world).
LAYERS = [
    ("Grass", "T_Grass_D", "T_Grass_N", (0.117, 0.133, 0.041), 4.0),
    ("DryGrass", "T_DriedGrass_D", "T_DriedGrass_N", (0.205, 0.154, 0.072), 4.0),
    ("Forest", "T_ForestGround_D", "T_ForestGround_N", (0.130, 0.109, 0.053), 4.0),
    ("Alpine", "T_ForestGround_Alpine_D", "T_ForestGround_N", (0.130, 0.109, 0.053), 4.0),
    ("Dirt", "T_DirtDry_D", "T_DirtDry_N", (0.180, 0.117, 0.062), 4.0),
    ("Sand", "T_Trampled_Sand_D", "T_Trampled_Sand_N", (0.302, 0.247, 0.177), 4.0),
    ("Desert", "T_Sand_Rocky_D", "T_Sand_Rocky_N", (0.349, 0.184, 0.096), 8.0),
    ("Scree", "T_RockyGround_D", "T_RockyGround_N", (0.139, 0.106, 0.072), 4.0),
    ("Snow", "T_Snowy_Rocky_Dirt_D", "T_Snowy_Rocky_Dirt_N", (0.421, 0.415, 0.431), 8.0),
    ("Rock", "T_RockyCliff_D", "T_RockyCliff_N", (0.130, 0.111, 0.087), 8.0),
]
GROUND = 8  # the first 8 layers are the ground; then the snow and the rock

# Tints of the layers (linear multipliers), tunable on the instances.
TINTS = {
    "Grass": (0.8, 1.1, 0.6), "DryGrass": (1.0, 0.95, 0.75), "Forest": (0.75, 0.95, 0.7),
    "Alpine": (0.85, 1.05, 0.85), "Dirt": (1.0, 0.95, 0.9), "Sand": (1.3, 1.25, 1.12),
    "Desert": (0.95, 0.95, 1.0), "Scree": (0.85, 0.95, 1.1), "Snow": (1.0, 1.0, 1.0),
    "Rock": (0.75, 0.9, 1.2),
}

SCALARS = {
    "RockStartDeg": 36.0, "RockFullDeg": 50.0, "SnowHoldDeg": 32.0, "SnowShedDeg": 44.0,
    "DetailNearM": 150.0, "DetailFarM": 400.0, "Saturation": 1.2, "NormalStrength": 0.8,
    "SnowWhiten": 0.7, "MacroVariation": 1.0, "CoreRock": 0.8, "DebugView": 0.0,
}
WARMTH = (1.03, 1.0, 0.95)

# Ground weights (Grass, DryGrass, Forest, Alpine | Dirt, Sand, Desert, Scree) and (cold, rock) of
# each biome of the core (terrain_core biome.hpp, in order), then the overrides of its materials.
BIOMES = [
    ((0, 0, 0, 0), (0, 1, 0, 0), (0, 0)),          # ocean: sand under the shallows
    ((0, 0, 0, 0), (0, 1, 0, 0), (0, 0)),          # beach
    ((0, 0, 0, 0), (0, 0, 1, 0), (0, 0)),          # desert
    ((0.2, 0.8, 0, 0), (0, 0, 0, 0), (0, 0)),      # savanna
    ((0.75, 0.25, 0, 0), (0, 0, 0, 0), (0, 0)),    # grassland
    ((0.35, 0, 0.65, 0), (0, 0, 0, 0), (0, 0)),    # temperate forest
    ((0.5, 0, 0.5, 0), (0, 0, 0, 0), (0, 0)),      # tropical forest
    ((0, 0, 0.6, 0.4), (0, 0, 0, 0), (0, 0)),      # taiga
    ((0, 0, 0, 0.7), (0.3, 0, 0, 0), (0, 0)),      # tundra
    ((0, 0, 0, 0.6), (0, 0, 0, 0.4), (0, 0)),      # mountain
    ((0, 0, 0, 0.4), (0, 0, 0, 0.6), (1, 0)),      # snow
    ((0.6, 0, 0, 0), (0.4, 0, 0, 0), (0, 0)),      # marsh
]

STRUCT_HLSL = r"""
struct FTemporaryTerrain
{
    // The layers of a texel: its packed surface (material + 8 biome, Bridge/terrain_surface.hpp)
    // or, on the ocean, a negative ground.
    void Layers(float code, out float4 a, out float4 b, out float2 c)
    {
        const float4 BA[12] = { BIOME_A };
        const float4 BB[12] = { BIOME_B };
        const float2 BC[12] = { BIOME_C };
        int v = code < 0.0 ? 0 : (int)(code + 0.5);
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

    float3 Saturate(float3 c, float s)
    {
        float l = dot(c, float3(0.2126, 0.7152, 0.0722));
        return max(lerp(l.xxx, c, s), 0.0);
    }
};
FTemporaryTerrain Look;
"""


def struct_hlsl():
    """The helper struct, its biome tables filled from BIOMES."""
    def rows(index, width):
        values = []
        for biome in BIOMES:
            cells = ", ".join(f"{float(x):.2f}" for x in biome[index])
            values.append(f"float{width}({cells})")
        return ", ".join(values)
    return (STRUCT_HLSL.replace("BIOME_A", rows(0, 4)).replace("BIOME_B", rows(1, 4))
            .replace("BIOME_C", rows(2, 2)))


def layers_of(texel, out_a, out_b, out_c):
    """HLSL: the layers of a texel (float4 of the horizon textures) into three variables."""
    return f"Look.Layers({texel}.a, {out_a}, {out_b}, {out_c});\n"


def texture_paths(pack):
    """Per layer: (albedo path, normal path, fallback tint) with or without the pack."""
    result = []
    for name, albedo, normal, mean, _ in LAYERS:
        tint = TINTS[name]
        if pack:
            result.append((STYLESCAPE + albedo, STYLESCAPE + normal, tint))
        else:
            result.append((WHITE, FLAT_NORMAL, tuple(t * m for t, m in zip(tint, mean))))
    return result


def clear_material(material):
    """Deletes every expression of a material rebuilt in place: delete_all_material_expressions
    only deletes about half of them per call (UE 5.8), and the remaining parameters of the same
    name would keep their old default values."""
    editing = unreal.MaterialEditingLibrary
    for _ in range(32):
        if editing.get_num_material_expressions(material) == 0:
            return
        editing.delete_all_material_expressions(material)
    raise RuntimeError(f"cannot delete the expressions of {material.get_name()}")


def has_pack():
    found = unreal.EditorAssetLibrary.does_asset_exist(STYLESCAPE + LAYERS[0][1])
    if not found:
        unreal.log_warning("temporary terrain material: no Stylescape HD pack under "
                           f"{STYLESCAPE}: flat colors instead of its textures")
    return found


# The world position in meters, periodic as the waves of the sea (4096 m, which divides each side
# of the wrapped world; WaveOriginX/Y follow the origin rebasing), and the distance to the camera.
POSITION_HLSL = r"""
const float PERIOD = 409600.0;
float2 w = GetTranslatedWorldPosition(Parameters).xy - GetPeriodicWorldOrigin(float3(PERIOD, PERIOD, PERIOD)).xy - float2(WaveOriginX, WaveOriginY);
Dist = length(CameraRel) / 100.0;
return w / 100.0;
"""

# The pixel shader of the ground (custom node): from the layer weights W0, W1, W2 (W2: cold, rock
# of the core, foot of steep slopes, unused), the world normal N, P the world position in meters
# (periodic, as the waves of the sea), Z the altitude in meters and Dist the distance to the
# camera: the base color (returned), the world normal and the roughness.
SHADE_TOP = r"""
float3 P3 = float3(P, Z);
float3 dPx = ddx(P3);
float3 dPy = ddy(P3);
float detail = HAS_DETAIL * saturate((DetailFarM - Dist) / max(DetailFarM - DetailNearM, 1.0));
float n1 = Look.Noise(P, 32.0);
float n2 = Look.Noise(P, 256.0);
float n3 = Look.Noise(P, 8.0);
float slope = degrees(acos(saturate(N.z)));
float jit = (n1 - 0.5) * 8.0 + (n3 - 0.5) * 3.0;
float rock = saturate((slope - (RockStartDeg + jit)) / max(RockFullDeg - RockStartDeg, 1.0));
rock = max(rock, W2.y * CoreRock);
float cold = saturate((W2.x - 0.5) * 2.5 + (n2 - 0.5) * 1.2 + 0.5);
float snow = cold * saturate((SnowShedDeg + jit - slope) / max(SnowShedDeg - SnowHoldDeg, 1.0));
float g[8] = { W0.x, W0.y, W0.z, W0.w, W1.x, W1.y, W1.z, W1.w };
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
DETAIL_CODE    ground += (g[I] / sum) * c * TintI;
}
"""

LAYER_DETAIL = r"""    BRANCH if (detail > 0.0)
    {
        float2 uv = P / SCALE;
        c = lerp(c, TAI.SampleGrad(TAISampler, uv, dPx.xy / SCALE, dPy.xy / SCALE).rgb, detail);
        if (g[I] > 0.35 * sum) { nts += (g[I] / sum) * (TNI.SampleGrad(TNISampler, uv, dPx.xy / SCALE, dPy.xy / SCALE).rg * 2.0 - 1.0); }
    }
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
    if (bw.x > 0.05) { c3 += bw.x * TAR.SampleGrad(TARSampler, P3.yz / SCALE, dPx.yz / SCALE, dPy.yz / SCALE).rgb;
        t = TNR.SampleGrad(TNRSampler, P3.yz / SCALE, dPx.yz / SCALE, dPy.yz / SCALE).rg * 2.0 - 1.0; dn += bw.x * float3(0.0, t.x, t.y); }
    if (bw.y > 0.05) { c3 += bw.y * TAR.SampleGrad(TARSampler, P3.xz / SCALE, dPx.xz / SCALE, dPy.xz / SCALE).rgb;
        t = TNR.SampleGrad(TNRSampler, P3.xz / SCALE, dPx.xz / SCALE, dPy.xz / SCALE).rg * 2.0 - 1.0; dn += bw.y * float3(t.x, 0.0, t.y); }
    if (bw.z > 0.05) { c3 += bw.z * TAR.SampleGrad(TARSampler, P3.xy / SCALE, dPx.xy / SCALE, dPy.xy / SCALE).rgb;
        t = TNR.SampleGrad(TNRSampler, P3.xy / SCALE, dPx.xy / SCALE, dPy.xy / SCALE).rg * 2.0 - 1.0; dn += bw.z * float3(t.x, t.y, 0.0); }
    rockC = lerp(rockC, c3, detail);
    rockN = normalize(N + dn * NormalStrength * detail);
}
"""

SNOW_DETAIL = r"""
BRANCH if (snow > 0.01 && detail > 0.0)
{
    snowC = lerp(snowC, TAS.SampleGrad(TASSampler, P / SCALE, dPx.xy / SCALE, dPy.xy / SCALE).rgb, detail);
}
"""

SHADE_BOTTOM = r"""
snowC = lerp(snowC * TintS, float3(0.80, 0.82, 0.86), SnowWhiten);
float3 groundN = normalize(float3(N.xy + nts * NormalStrength * detail, N.z));
float3 color = lerp(ground, rockC * TintR, rock);
float3 normal = normalize(lerp(groundN, rockN, rock));
color = lerp(color, snowC, snow);
normal = normalize(lerp(normal, N, snow * 0.7));
RoughOut = lerp(lerp(0.92, 0.82, rock), 0.6, snow);
float macro = 1.0 + MacroVariation * (0.22 * (n2 - 0.5) + 0.12 * (n1 - 0.5));
color = Look.Saturate(color * macro, Saturation) * Warmth;
if (DebugView > 0.5)
{
    const float3 K[8] = { float3(0.1, 0.6, 0.1), float3(0.8, 0.7, 0.2), float3(0.0, 0.25, 0.05),
        float3(0.2, 0.7, 0.6), float3(0.45, 0.25, 0.1), float3(0.9, 0.8, 0.6), float3(1.0, 0.45, 0.1),
        float3(0.5, 0.5, 0.5) };
    float3 d = float3(0.0, 0.0, 0.0);
    [unroll] for (int k = 0; k < 8; ++k) { d += (g[k] / sum) * K[k]; }
    d = lerp(lerp(d, float3(0.9, 0.1, 0.1), rock), float3(1.0, 1.0, 1.0), snow);
    color = DebugView < 1.5 ? d : float3(rock, snow, saturate(W2.z));
}
NormalOut = normal;
return color;
"""


def layer_inputs(detail):
    """Names of the texture and tint inputs of the shade node, in LAYERS order."""
    names = []
    for index in range(len(LAYERS)):
        names.append(f"TA{index}")
        if detail:
            names.append(f"TN{index}")
    names += [f"Tint{index}" for index in range(len(LAYERS))]
    return names


def shade_inputs():
    """Every input of the shade node besides the textures and tints."""
    return ["W0", "W1", "W2", "N", "P", "Z", "Dist", "Warmth"] + list(SCALARS)


def shade_hlsl(detail):
    """The pixel code: with detail (tiles), the textures near the camera; without (horizon), only
    the mean color of each texture."""
    code = SHADE_TOP.replace("HAS_DETAIL", "1.0" if detail else "0.0")
    for index in range(GROUND):
        scale = f"{LAYERS[index][4]:.1f}"
        layer = LAYER_FAR.replace("DETAIL_CODE", LAYER_DETAIL if detail else "")
        code += layer.replace("TAI", f"TA{index}").replace("TNI", f"TN{index}").replace(
            "TintI", f"Tint{index}").replace("[I]", f"[{index}]").replace("SCALE", scale)
    snow, rock = GROUND, GROUND + 1
    code += (f"float3 rockC = TA{rock}.SampleLevel(TA{rock}Sampler, P / {LAYERS[rock][4]:.1f}, 13.0).rgb;\n"
             "float3 rockN = N;\n"
             f"float3 snowC = TA{snow}.SampleLevel(TA{snow}Sampler, P / {LAYERS[snow][4]:.1f}, 13.0).rgb;\n")
    if detail:
        code += ROCK_DETAIL.replace("TAR", f"TA{rock}").replace("TNR", f"TN{rock}").replace(
            "SCALE", f"{LAYERS[rock][4]:.1f}")
        code += SNOW_DETAIL.replace("TAS", f"TA{snow}").replace("SCALE", f"{LAYERS[snow][4]:.1f}")
    code += SHADE_BOTTOM.replace("TintS", f"Tint{snow}").replace("TintR", f"Tint{rock}")
    return struct_hlsl() + code


def texture_parameter(material, name, path, x, y, normal=False):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureObjectParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("texture", unreal.EditorAssetLibrary.load_asset(path))
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal
                             else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    node.set_editor_property("sampler_source",
                             unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    return node


def vector_parameter(material, name, value, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
    return node


def look_parameters(material, detail, x, y):
    """The parameters of the look, by input name of the shade node: textures, tints, scalars and
    the warmth. Without the Stylescape pack, white textures tinted with the mean colors."""
    pack = has_pack()
    nodes = {}
    for index, (albedo, normal, tint) in enumerate(texture_paths(pack)):
        name = LAYERS[index][0]
        nodes[f"TA{index}"] = texture_parameter(material, f"{name}Albedo", albedo, x, y)
        if detail:
            nodes[f"TN{index}"] = texture_parameter(material, f"{name}Normal", normal, x, y + 130,
                                                    True)
        nodes[f"Tint{index}"] = vector_parameter(material, f"{name}Tint", tint, x + 300, y)
        y += 260
    for name, value in SCALARS.items():
        scalar = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionScalarParameter, x + 300, y)
        scalar.set_editor_property("parameter_name", name)
        scalar.set_editor_property("default_value", value)
        nodes[name] = scalar
        y += 60
    nodes["Warmth"] = vector_parameter(material, "Warmth", WARMTH, x + 300, y)
    return nodes
