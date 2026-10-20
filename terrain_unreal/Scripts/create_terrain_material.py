"""Creates /Game/TerrainEngine/M_TerrainAltitude in the open Unreal project.

A lit material whose base color is the vertex color of the terrain tiles (the altitude color the
plugin computes, Bridge/altitude_color.hpp), rough and not metallic: the relief reads at once,
lit by the sun of the level. ATerrainWorld uses it when its Material property is empty.

The asset belongs to the game project, not to this repository. Run the script once per project,
in the Unreal Editor (the Python Editor Script Plugin must be enabled):
    Tools > Execute Python Script... > terrain_unreal/Scripts/create_terrain_material.py
or in the Output Log, with the command field set to Python:
    py "C:/path/to/terrain-engine/terrain_unreal/Scripts/create_terrain_material.py"
"""

import unreal

PACKAGE_PATH = "/Game/TerrainEngine"
ASSET_NAME = "M_TerrainAltitude"
ROUGHNESS = 0.9


def create_terrain_material():
    asset_path = f"{PACKAGE_PATH}/{ASSET_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log(f"{asset_path} already exists: nothing to do")
        return unreal.EditorAssetLibrary.load_asset(asset_path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(ASSET_NAME, PACKAGE_PATH, unreal.Material,
                                  unreal.MaterialFactoryNew())
    editing = unreal.MaterialEditingLibrary

    color = editing.create_material_expression(material, unreal.MaterialExpressionVertexColor,
                                               -400, 0)
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant,
                                                   -400, 200)
    roughness.set_editor_property("r", ROUGHNESS)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.log(f"{asset_path} created")
    return material


create_terrain_material()
