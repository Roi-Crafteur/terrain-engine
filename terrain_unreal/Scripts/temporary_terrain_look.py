"""The TEMPORARY terrain material of phase 14: a readable stand-in until the real materials.

Shared by create_temporary_terrain_material.py (the tiles, /Game/TerrainEngine/M_TemporaryTerrain)
and create_horizon_gpu_material.py (the far horizon), so that the near and far ground take their
layers by the same rules; its HLSL is in temporary_terrain_hlsl.py. To replace it, swap these two
modules and the tile material; nothing in the C++ depends on the layers.

Rules (kept simple on purpose):
- the material and the biome of the core (packed in the texels of the horizon,
  Bridge/terrain_surface.hpp) pick the ground layers: grass, dry grass, forest floor, alpine
  grass, dirt, sand, desert, scree, with soft transitions (blended per vertex between samples);
  on the tiles, the weights of the 2 or 3 dominant biomes and materials of the core
  (TileData::surfaceBlend) blend them, each weight added to its id, never interpolating ids;
- patches within a biome, as in Minecraft (world noise, PatchAmount): dry grass and darker grass
  in the meadows, scree and a second rock in the rock, rock outcrops in the snow;
- rare bare earth and gravel that follow the relief (the curvature of the ground on the tiles,
  CurvatureGain): gravel in the hollows and at the foot of slopes, bare earth on bumps, crests and
  moderate slopes, more gravel in the desert; PatchRelief of the site mixed with a noise, above
  PatchRarity;
- rock from about RockStartDeg of slope, fully rock from RockFullDeg, the threshold jittered by
  noise, and wherever the core puts rock (CoreRock of it, at every distance): a cold grey mineral
  (its textures desaturated by RockGrey, brightened by RockAlbedo to about 0.18), never sandy;
- snow where the core says it is cold (snow biome or material, which includes the altitude and the
  exposure), only on slopes gentler than SnowShedDeg: above the snow line, the faces too steep for
  the snow are rock;
- soil on moderate slopes (dirt under grass and forest, scree under alpine grass), scree at the
  foot of steep slopes (tiles: the steepest of the 4 texels around a vertex);
- world-aligned textures (top projection for the ground, triplanar for the rock), sampled with
  stochastic tiling (no readable repetition), faded beyond DetailNearM..DetailFarM to the mean
  color of each texture (its last mip), which is all the horizon uses: near tiles, far tiles and
  the horizon meet without a seam;
- a large-scale variation of tone and hue (50 to 500 m), then saturation and warmth for the ground
  only (the rock stays cold grey, the snow clean white).

Textures: the Stylescape HD pack (Fab license: never in the git repository) under
/Game/StylescapeHD in the game project. Without it, every layer falls back to a flat color (the
mean of its texture): the scripts still build working materials.
"""

import importlib

import unreal

import temporary_terrain_hlsl as hlsl
hlsl = importlib.reload(hlsl)  # the module of this checkout, even if run before

STYLESCAPE = "/Game/StylescapeHD/"
WHITE = "/Engine/EngineResources/WhiteSquareTexture"
FLAT_NORMAL = "/Engine/EngineMaterials/FlatNormal"
LANDSCAPE = "Landscape/Textures/"
CLIFF = "Foliage/Textures/Cliff/"


def _layer(name, base, normal, mean, scale, folder=LANDSCAPE):
    return (name, f"{folder}{base}_D", f"{folder}{normal}_N", mean, scale)


# (name, albedo, normal, fallback linear color = the mean of the albedo, meters per repeat)
# Repeats divide 4096 m, the period of the world-aligned coordinates (they also divide each
# side of the wrapped world). The 8 ground layers, then the snow, the rock and its variant.
LAYERS = [
    _layer("Grass", "T_Grass", "T_Grass", (0.117, 0.133, 0.041), 4.0),
    _layer("DryGrass", "T_DriedGrass", "T_DriedGrass", (0.205, 0.154, 0.072), 4.0),
    _layer("Forest", "T_ForestGround", "T_ForestGround", (0.130, 0.109, 0.053), 4.0),
    _layer("Alpine", "T_ForestGround_Alpine", "T_ForestGround", (0.130, 0.109, 0.053), 4.0),
    _layer("Dirt", "T_DirtDry", "T_DirtDry", (0.180, 0.117, 0.062), 4.0),
    _layer("Sand", "T_Trampled_Sand", "T_Trampled_Sand", (0.302, 0.247, 0.177), 4.0),
    _layer("Desert", "T_Sand_Rocky", "T_Sand_Rocky", (0.349, 0.184, 0.096), 8.0),
    _layer("Scree", "T_RockyGround", "T_RockyGround", (0.139, 0.106, 0.072), 4.0),
    _layer("Snow", "T_Snowy_Rocky_Dirt", "T_Snowy_Rocky_Dirt", (0.421, 0.415, 0.431), 8.0),
    _layer("Rock", "T_RockyCliff", "T_RockyCliff", (0.130, 0.111, 0.087), 8.0),
    _layer("RockAlt", "T_Cliff", "T_Cliff", (0.359, 0.325, 0.263), 8.0, CLIFF),
]
GROUND = 8

# Tints of the layers (linear multipliers), tunable on the instances.
TINTS = {
    "Grass": (0.8, 1.1, 0.6), "DryGrass": (1.0, 0.95, 0.75), "Forest": (0.75, 0.95, 0.7),
    "Alpine": (0.85, 1.05, 0.85), "Dirt": (1.0, 0.95, 0.9), "Sand": (1.3, 1.25, 1.12),
    "Desert": (0.95, 0.95, 1.0), "Scree": (0.85, 0.94, 1.08), "Snow": (0.9, 0.95, 1.04),
    "Rock": (0.82, 0.93, 1.12), "RockAlt": (0.34, 0.39, 0.46),
}
# The mineral layers (scree, snow, rock) are cold on purpose: under the orange light of a low sun
# they must stay grey and white, never the tan of sand.
# Layers drawn less saturated than their texture: the scree is grey stone, the dirt brown earth,
# neither red.
LAYER_SATURATION = {"Scree": 0.35, "Dirt": 0.7}

SCALARS = {
    "RockStartDeg": 34.0, "RockFullDeg": 46.0, "SnowHoldDeg": 22.0, "SnowShedDeg": 33.0,
    "DetailNearM": 150.0, "DetailFarM": 400.0, "Saturation": 1.2, "NormalStrength": 0.8,
    "SnowWhiten": 0.92, "MacroVariation": 1.0, "PatchAmount": 1.0, "CoreRock": 0.8,
    "RockGrey": 0.85, "RockAlbedo": 1.15, "RockContrast": 1.4, "SandMaxAltitudeM": 1800.0,
    "PatchRarity": 0.6, "PatchRelief": 0.45, "CurvatureGain": 50.0, "DebugView": 0.0,
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

POSITION_HLSL = hlsl.POSITION_HLSL


def struct_hlsl():
    """The helper struct of the HLSL, its biome tables filled from BIOMES."""
    return hlsl.struct_hlsl(BIOMES)


def shade_hlsl(detail):
    """The pixel code: with detail (tiles), the textures near the camera; without (horizon), only
    the mean color of each texture."""
    return hlsl.shade_hlsl(detail, LAYERS, GROUND, LAYER_SATURATION, BIOMES)


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
    exists = unreal.EditorAssetLibrary.does_asset_exist
    found = all(exists(STYLESCAPE + layer[1]) for layer in LAYERS)
    if not found:
        unreal.log_warning("temporary terrain material: no Stylescape HD pack under "
                           f"{STYLESCAPE}: flat colors instead of its textures")
    return found


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
