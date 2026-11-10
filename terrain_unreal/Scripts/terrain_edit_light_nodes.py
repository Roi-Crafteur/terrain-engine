"""The light floor of the edited surfaces in M_TemporaryTerrain (agent 41), without HLSL.

The surfaces the edits of the players made emit EditLightFloor times their base color:
    emissive = base color x EditLightFloor x EditLightOn x EditedTile x B
- B: the blue of the vertex color, the edit weight of the vertex on an edited tile (1 on the floor
  and the walls of a hole, 0 on the ground the edits left: TileData::editWeights,
  Bridge/tile_mesh_conversion.hpp); the blue of the altitude color on the other tiles, which
  EditedTile then zeroes.
- EditedTile: custom primitive data 2 of the tile (ProceduralMeshTerrainBackend), 1 on a tile whose
  colors hold the edit weights, 0 on the others.
- EditLightOn: 0 by default, the floor off; Terrain.EditLight 1 or Building.bEditLightFloor turn it
  on (TerrainWorldEditLight.cpp), EditLightFloor the share (Terrain.EditLightFloor, 0.15 by
  default).
No other node of the tile materials reads the blue of the vertex color
(unreal_edit_light_scripts_test.py). Called by create_temporary_terrain_material.py.
"""

import unreal

# The parameters of the material: name -> default (the floor off).
SCALARS = {"EditLightOn": 0.0, "EditLightFloor": 0.15}
# The custom primitive data of a tile marking it as edited: name, index, default.
EDITED_TILE = ("EditedTile", 2, 0.0)


def _expression(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def _connect(source, source_output, target, target_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, source_output, target, target_input):
        raise RuntimeError(f"edit light: cannot connect {source_output} to {target_input}")


def _scalar(material, name, value, x, y):
    node = _expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def _multiply(material, a, a_output, b, b_output, x, y):
    node = _expression(material, unreal.MaterialExpressionMultiply, x, y)
    _connect(a, a_output, node, "A")
    _connect(b, b_output, node, "B")
    return node


def add_edit_light(material, base_color, x=-300, y=1400):
    """The emissive of the edited surfaces from base_color (its first output), returned."""
    on = _scalar(material, "EditLightOn", SCALARS["EditLightOn"], x - 900, y)
    share = _scalar(material, "EditLightFloor", SCALARS["EditLightFloor"], x - 900, y + 80)
    name, index, value = EDITED_TILE
    edited = _scalar(material, name, value, x - 900, y + 160)
    edited.set_editor_property("use_custom_primitive_data", True)
    edited.set_editor_property("primitive_data_index", index)
    color = _expression(material, unreal.MaterialExpressionVertexColor, x - 900, y + 240)
    gate = _multiply(material, on, "", share, "", x - 650, y + 40)
    tile = _multiply(material, gate, "", edited, "", x - 450, y + 100)
    weight = _multiply(material, tile, "", color, "B", x - 250, y + 160)
    emissive = _multiply(material, base_color, "", weight, "", x, y)
    unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return emissive
