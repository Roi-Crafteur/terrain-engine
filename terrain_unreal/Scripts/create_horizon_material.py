"""Creates /Game/TerrainEngine/M_TerrainHorizon in the open Unreal project.

The material of the far horizon (phase 13): lit, its base color the vertex color (the altitude
colors of the tiles, Bridge/altitude_color.hpp), rough and not metallic, as M_TerrainAltitude;
and the curvature of the planet. Its World Position Offset moves every vertex down by d^2 / 2R,
d being its horizontal distance to the camera (the camera relative world position, precise far
from the origin) and R the scalar parameter PlanetRadiusCm, which ATerrainWorld sets from the
circumference of the world (about 9,554 km). Both sides of a seam between two levels share their
vertices, so they fall by the same height: the curvature leaves no crack.

The asset belongs to the game project, not to this repository. Run the script once per project,
in the Unreal Editor (the Python Editor Script Plugin must be enabled):
    Tools > Execute Python Script... > terrain_unreal/Scripts/create_horizon_material.py
or in the Output Log, with the command field set to Python:
    py "C:/path/to/terrain-engine/terrain_unreal/Scripts/create_horizon_material.py"
"""

import math

import unreal

PACKAGE_PATH = "/Game/TerrainEngine"
ASSET_NAME = "M_TerrainHorizon"
ROUGHNESS = 0.9
RADIUS_PARAMETER = "PlanetRadiusCm"
# The default world: 60,030,976 m around along x.
DEFAULT_RADIUS_CM = 60_030_976.0 / (2.0 * math.pi) * 100.0


def expression(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def connect(source, source_output, target, target_input):
    unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target,
                                                               target_input)


def curvature_offset(material):
    """The World Position Offset (0, 0, -(x^2 + y^2) / 2R), x and y relative to the camera."""
    position = expression(material, unreal.MaterialExpressionWorldPosition, -1400, 300)
    position.set_editor_property(
        "world_position_shader_offset",
        unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE_NO_OFFSETS)
    horizontal = expression(material, unreal.MaterialExpressionComponentMask, -1200, 300)
    horizontal.set_editor_property("r", True)
    horizontal.set_editor_property("g", True)
    connect(position, "", horizontal, "")
    squared = expression(material, unreal.MaterialExpressionDotProduct, -1000, 300)
    connect(horizontal, "", squared, "A")
    connect(horizontal, "", squared, "B")
    radius = expression(material, unreal.MaterialExpressionScalarParameter, -1200, 500)
    radius.set_editor_property("parameter_name", RADIUS_PARAMETER)
    radius.set_editor_property("default_value", DEFAULT_RADIUS_CM)
    diameter = expression(material, unreal.MaterialExpressionMultiply, -1000, 500)
    diameter.set_editor_property("const_b", 2.0)
    connect(radius, "", diameter, "A")
    drop = expression(material, unreal.MaterialExpressionDivide, -800, 300)
    connect(squared, "", drop, "A")
    connect(diameter, "", drop, "B")
    down = expression(material, unreal.MaterialExpressionMultiply, -600, 300)
    down.set_editor_property("const_b", -1.0)
    connect(drop, "", down, "A")
    flat = expression(material, unreal.MaterialExpressionConstant2Vector, -600, 450)
    offset = expression(material, unreal.MaterialExpressionAppendVector, -400, 300)
    connect(flat, "", offset, "A")
    connect(down, "", offset, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def create_horizon_material():
    asset_path = f"{PACKAGE_PATH}/{ASSET_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log(f"{asset_path} already exists: nothing to do")
        return unreal.EditorAssetLibrary.load_asset(asset_path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(ASSET_NAME, PACKAGE_PATH, unreal.Material,
                                  unreal.MaterialFactoryNew())
    editing = unreal.MaterialEditingLibrary

    color = expression(material, unreal.MaterialExpressionVertexColor, -400, 0)
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = expression(material, unreal.MaterialExpressionConstant, -400, 150)
    roughness.set_editor_property("r", ROUGHNESS)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    curvature_offset(material)

    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.log(f"{asset_path} created")
    return material


create_horizon_material()
