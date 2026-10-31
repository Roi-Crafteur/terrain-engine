"""Creates (or rebuilds) the materials of the water of the tiles (agent 27) in the open Unreal
project: /Game/TerrainEngine/M_TerrainWater and /Game/TerrainEngine/M_TerrainFalls.

M_TerrainWater draws the surface of the lakes and rivers of the tiles (section 1 of their meshes,
Bridge/tile_water_mesh.hpp), already at the level of the water: translucent, lit per pixel
(Surface ForwardShading), its color and transparency by the thickness of water behind each pixel
(scene depth minus pixel depth) and by its kind and place, ripples carried by the flow of the
rivers, the reflection of the sky and the glint of the sun, foam on the rapids
(water_look_hlsl.py: the same look as the lakes and rivers of the far horizon), faded out toward
the cuts at the falls, at the shores, and over the outer band of the tiles where the GPU horizon
dithers its own water in (it places that band: SurfaceOrigin, SurfaceCells, SurfacePitchCm,
SurfaceCoveredCm, SurfaceBandCm, HasSurface, as on the material of the ground of the tiles). Its
own material and parameters: it can become a Single Layer Water material later (phase 14,
WATER_LEVEL.md) without touching the rest.

M_TerrainFalls draws the curtains of the falls and the foam at their feet (section 2,
Bridge/fall_curtains.hpp): translucent, two sided, streaks falling with the water, soft where they
meet the rock or the water; the foam full, its rim ragged and soft, thinner where the pool under it
gets shallow (FoamDepthM), so that it takes the shape of the pool.

The UV channels of the meshes: UV0 the flow (or meters across and down a curtain), UV1 what the
water is (1 river, 2 lake, 3 salt lake, 4 curtain, 5 foam) and its slope (or the height of the
fall), UV2 its altitude and, on the surface, its edge (0 at the corners of the cells cut at a fall),
on a curtain its width, on the foam its radius. ATerrainWorld finds the materials by these paths
(WaterMaterial, FallsMaterial) and makes an instance of M_TerrainWater for the horizon to place.

The assets belong to the game project, not to this repository. Run the script once per project,
and again after updating the plugin, in the Unreal Editor (the Python Editor Script Plugin must be
enabled); it rebuilds existing assets in place, every setting reset (material_settings.py):
    Tools > Execute Python Script... > terrain_unreal/Scripts/create_water_materials.py
No texture: nothing to license.
"""

import importlib
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import material_settings as settings  # noqa: E402  (after the path of this script)
import temporary_terrain_look as look  # noqa: E402
import water_look_hlsl as water  # noqa: E402
settings = importlib.reload(settings)  # the modules of this checkout, even if run before
look = importlib.reload(look)
water = importlib.reload(water)

PACKAGE_PATH = "/Game/TerrainEngine"
FLOAT1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
FLOAT2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
FLOAT3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
# The inputs of the nodes of the two materials, besides the look of the water.
WATER_INPUTS = ["Flow", "KindSlope", "Altitude", "Edge", "CameraRel", "Time", "SunDir",
                "SunIlluminance", "SkyView", "SceneDepth", "PixelDepth", "VertexNormal", "Rel"]
# The band of the tiles over level 0 of the GPU horizon, set by it (TerrainHorizonClipmap.cpp);
# HasSurface 0 without the horizon: no fade.
SURFACE_SCALARS = {"SurfaceCells": 256.0, "SurfacePitchCm": 800.0, "SurfaceCoveredCm": 0.0,
                   "SurfaceBandCm": 0.0, "HasSurface": 0.0}
FALLS_INPUTS = ["Fall", "KindDrop", "AltitudeSize", "Time", "SceneDepth", "PixelDepth",
                "CurtainColor", "FoamColor", "FoamDepthM"]


def expression(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def connect(source, source_output, target, target_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, source_output, target, target_input):
        raise RuntimeError(f"cannot connect {source_output} to {target_input}")


def custom_node(material, x, y, description, code, inputs, outputs, output_type):
    """A Custom node: its inputs by name, its additional outputs as (name, output type)."""
    custom = expression(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("code", code)
    custom.set_editor_property("output_type", output_type)
    custom.set_editor_property("description", description)
    custom_inputs = []
    for name in inputs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)
    custom_outputs = []
    for name, kind in outputs:
        output = unreal.CustomOutput()
        output.set_editor_property("output_name", name)
        output.set_editor_property("output_type", kind)
        custom_outputs.append(output)
    custom.set_editor_property("additional_outputs", custom_outputs)
    return custom


def look_parameters(material, names, x, y):
    """The parameters of the look of the water among names, by name."""
    nodes = {}
    for name, value in water.WATER_COLORS.items():
        if name in names:
            node = expression(material, unreal.MaterialExpressionVectorParameter, x, y)
            node.set_editor_property("parameter_name", name)
            node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
            nodes[name] = node
            y += 120
    for name, value in water.WATER_SCALARS.items():
        if name in names:
            node = expression(material, unreal.MaterialExpressionScalarParameter, x, y)
            node.set_editor_property("parameter_name", name)
            node.set_editor_property("default_value", value)
            nodes[name] = node
            y += 60
    return nodes


def uv(material, index, channels, x, y):
    """A channel of the UVs of the vertex (index 0 to 2), masked to channels."""
    node = expression(material, unreal.MaterialExpressionTextureCoordinate, x, y)
    node.set_editor_property("coordinate_index", index)
    if channels == "rg":
        return node
    masked = expression(material, unreal.MaterialExpressionComponentMask, x + 150, y)
    for channel in "rgba":
        masked.set_editor_property(channel, channel in channels)
    connect(node, "", masked, "")
    return masked


def depths(material, custom, x, y):
    """The scene depth behind the pixel and its own depth, centimeters."""
    connect(expression(material, unreal.MaterialExpressionSceneDepth, x, y), "", custom,
            "SceneDepth")
    connect(expression(material, unreal.MaterialExpressionPixelDepth, x, y + 100), "", custom,
            "PixelDepth")


def sky(material, custom, x, y):
    """The time, the sun and the sky view LUT (unused by the code: it binds the LUT)."""
    for cls, name, dy in ((unreal.MaterialExpressionTime, "Time", 0),
                          (unreal.MaterialExpressionSkyAtmosphereLightDirection, "SunDir", 100),
                          (unreal.MaterialExpressionSkyAtmosphereLightIlluminance,
                           "SunIlluminance", 200),
                          (unreal.MaterialExpressionSkyAtmosphereViewLuminance, "SkyView", 300)):
        connect(expression(material, cls, x, y + dy), "", custom, name)


def surface_band(material, custom, x, y):
    """Where the pixel stands from the corner of level 0 of the horizon (Rel, cm) and the band of
    the tiles over it, placed by the horizon."""
    origin = expression(material, unreal.MaterialExpressionDoubleVectorParameter, x - 300, y)
    origin.set_editor_property("parameter_name", "SurfaceOrigin")
    position = expression(material, unreal.MaterialExpressionWorldPosition, x - 300, y + 150)
    relative = expression(material, unreal.MaterialExpressionSubtract, x, y + 50)
    connect(position, "", relative, "A")
    connect(origin, "", relative, "B")
    to_float = expression(material, unreal.MaterialExpressionComponentMask, x + 150, y + 50)
    for channel in "rgba":
        to_float.set_editor_property(channel, channel != "a")
    connect(relative, "", to_float, "")
    connect(to_float, "", custom, "Rel")
    for index, (name, value) in enumerate(SURFACE_SCALARS.items()):
        node = expression(material, unreal.MaterialExpressionScalarParameter, x,
                          y + 250 + 60 * index)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        connect(node, "", custom, name)


def water_graph(material):
    """M_TerrainWater: the surface of the lakes and rivers of the tiles."""
    inputs = WATER_INPUTS + list(SURFACE_SCALARS) + water.look_inputs()
    custom = custom_node(material, -300, 0, "TerrainWater",
                         water.struct_hlsl() + water.tile_water_hlsl(), inputs,
                         [("EmissiveOut", FLOAT3), ("OpacityOut", FLOAT1), ("NormalOut", FLOAT3),
                          ("RoughnessOut", FLOAT1)], FLOAT3)
    connect(uv(material, 0, "rg", -900, -400), "", custom, "Flow")
    connect(uv(material, 1, "rg", -900, -300), "", custom, "KindSlope")
    connect(uv(material, 2, "r", -900, -200), "", custom, "Altitude")
    connect(uv(material, 2, "g", -900, -150), "", custom, "Edge")
    camera = expression(material, unreal.MaterialExpressionWorldPosition, -900, -100)
    camera.set_editor_property("world_position_shader_offset",
                               unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    connect(camera, "", custom, "CameraRel")
    sky(material, custom, -900, 0)
    depths(material, custom, -900, 400)
    connect(expression(material, unreal.MaterialExpressionVertexNormalWS, -900, 600), "", custom,
            "VertexNormal")
    surface_band(material, custom, -900, 800)
    for name, node in look_parameters(material, water.look_inputs(), -1300, -400).items():
        connect(node, "", custom, name)
    specular = expression(material, unreal.MaterialExpressionConstant, 0, 400)
    specular.set_editor_property("r", 0.0)
    editing = unreal.MaterialEditingLibrary
    editing.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(custom, "EmissiveOut",
                                      unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    editing.connect_material_property(custom, "OpacityOut", unreal.MaterialProperty.MP_OPACITY)
    editing.connect_material_property(custom, "NormalOut", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(custom, "RoughnessOut",
                                      unreal.MaterialProperty.MP_ROUGHNESS)
    editing.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)


def falls_graph(material):
    """M_TerrainFalls: the curtains of the falls and the foam at their feet."""
    inputs = FALLS_INPUTS
    custom = custom_node(material, -300, 0, "TerrainFalls",
                         water.struct_hlsl() + water.FALLS_HLSL, inputs,
                         [("OpacityOut", FLOAT1)], FLOAT3)
    connect(uv(material, 0, "rg", -900, -300), "", custom, "Fall")
    connect(uv(material, 1, "rg", -900, -200), "", custom, "KindDrop")
    connect(uv(material, 2, "rg", -900, -100), "", custom, "AltitudeSize")
    connect(expression(material, unreal.MaterialExpressionTime, -900, 0), "", custom, "Time")
    depths(material, custom, -900, 100)
    for name, node in look_parameters(material, inputs, -1300, -300).items():
        connect(node, "", custom, name)
    roughness = expression(material, unreal.MaterialExpressionConstant, 0, 300)
    roughness.set_editor_property("r", 0.5)
    editing = unreal.MaterialEditingLibrary
    editing.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(custom, "OpacityOut", unreal.MaterialProperty.MP_OPACITY)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)


def create_material(name, graph, **wanted):
    asset_path = f"{PACKAGE_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        material = unreal.EditorAssetLibrary.load_asset(asset_path)
        look.clear_material(material)
        unreal.log(f"{asset_path} exists: rebuilt in place")
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        material = tools.create_asset(name, PACKAGE_PATH, unreal.Material,
                                      unreal.MaterialFactoryNew())
    settings.reset_material(material, **wanted)
    graph(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.log(f"{asset_path} created")
    return material


def create_water_materials():
    translucent = {"blend_mode": unreal.BlendMode.BLEND_TRANSLUCENT,
                   "translucency_lighting_mode":
                       unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING}
    # The normal of the water is in world space (the ripples on the normal of the mesh).
    create_material("M_TerrainWater", water_graph, tangent_space_normal=False, **translucent)
    create_material("M_TerrainFalls", falls_graph, two_sided=True, **translucent)


create_water_materials()
