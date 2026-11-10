"""The light floor of the edited surfaces in the scripts of the materials of Unreal (agent 41),
without Unreal.

- terrain_edit_light_nodes.py: the floor off by default (EditLightOn 0), its share the default of
  FTerrainBuildSettings::EditLightFloor, bEditLightFloor false there, and the custom primitive data
  of an edited tile at the index ProceduralMeshTerrainBackend.cpp writes (EditedTileData).
- Its nodes, built on a stand-in of the unreal module: the emissive of the material is the
  product of the base color, EditLightOn, EditLightFloor, EditedTile (custom primitive data) and
  the blue of the vertex color, nothing else.
- The blue of the vertex colors of the tiles is read by the light floor only: M_TemporaryTerrain
  reads the alpha (the rock) and nothing else of its vertex color; the water and the falls read
  no vertex color. (M_TerrainAltitude, the fallback, reads the color as it is: an edited tile
  shows its edit weights in blue there, documented in BREAK_PLACE.md; the horizon materials read
  the colors of the horizon meshes, not those of the tiles.)
Usage: unreal_edit_light_scripts_test.py <terrain_unreal/Scripts> <terrain_unreal/.../Private>
"""

import os
import re
import sys
import types

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAILED:", message)


class Node:
    def __init__(self, cls):
        self.cls = cls
        self.properties = {}
        self.inputs = {}  # input name -> (source node, its output)

    def set_editor_property(self, name, value):
        self.properties[name] = value


def stand_in():
    """A module unreal that records the nodes and their links."""
    module = types.ModuleType("unreal")
    for name in ("MaterialExpressionScalarParameter", "MaterialExpressionMultiply",
                 "MaterialExpressionVertexColor"):
        setattr(module, name, name)
    module.MaterialProperty = types.SimpleNamespace(MP_EMISSIVE_COLOR="emissive")
    module.properties = {}

    def create(material, cls, x, y):
        return Node(cls)

    def connect(source, output, target, name):
        target.inputs[name] = (source, output)
        return True

    def connect_property(node, output, prop):
        module.properties[prop] = (node, output)

    module.MaterialEditingLibrary = types.SimpleNamespace(
        create_material_expression=create, connect_material_expressions=connect,
        connect_material_property=connect_property)
    return module


def leaves(node, output, base):
    """The factors of a product of Multiply nodes, as (what, output)."""
    if node is base:
        return [("base color", output)]
    if node.cls == "MaterialExpressionMultiply":
        return leaves(*node.inputs["A"], base) + leaves(*node.inputs["B"], base)
    if node.cls == "MaterialExpressionScalarParameter":
        name = node.properties["parameter_name"]
        if node.properties.get("use_custom_primitive_data"):
            return [(f"{name} (primitive data {node.properties['primitive_data_index']})",
                     output)]
        return [(name, output)]
    return [(node.cls, output)]


def check_nodes(scripts, private):
    sys.modules["unreal"] = stand_in()
    sys.path.insert(0, scripts)
    import terrain_edit_light_nodes as light

    edits = open(os.path.join(os.path.dirname(private), "Public", "TerrainEdits.h")).read()
    backend = open(os.path.join(private, "ProceduralMeshTerrainBackend.cpp")).read()
    check("bool bEditLightFloor = false;" in edits, "bEditLightFloor is false by default")
    share = re.search(r"float EditLightFloor = ([0-9.]+)f;", edits)
    check(share is not None and float(share.group(1)) == light.SCALARS["EditLightFloor"],
          "EditLightFloor of the material is the default of FTerrainBuildSettings")
    check(light.SCALARS["EditLightOn"] == 0.0, "the floor is off by default in the material")
    index = re.search(r"constexpr int32 EditedTileData = (\d+);", backend)
    check(index is not None and int(index.group(1)) == light.EDITED_TILE[1],
          "EditedTile reads the custom primitive data the backend writes")
    base = Node("shade")
    emissive = light.add_edit_light(object(), base)
    unreal = sys.modules["unreal"]
    check(unreal.properties.get("emissive") == (emissive, ""), "the emissive is the floor")
    factors = sorted(leaves(emissive, "", base))
    expected = sorted([("base color", ""), ("EditLightOn", ""), ("EditLightFloor", ""),
                       ("EditedTile (primitive data 2)", ""),
                       ("MaterialExpressionVertexColor", "B")])
    check(factors == expected, f"the emissive is the product of {expected}, not {factors}")


def vertex_color_reads(text):
    """The outputs of the vertex colors a script connects, by the variables holding them."""
    names = re.findall(r"(\w+) = \w*\(?(?:expression|_expression|editing\.create_material_"
                       r"expression)\(\s*material,\s*unreal\.MaterialExpressionVertexColor", text)
    reads = set()
    for name in names:
        reads.update(re.findall(r"connect\w*\(\s*" + name + r',\s*"(\w*)"', text))
        reads.update(re.findall(r"_multiply\([^)]*?\b" + name + r',\s*"(\w*)"', text))
    return reads


def check_readers(scripts):
    def read(name):
        return open(os.path.join(scripts, name)).read()

    check(vertex_color_reads(read("create_temporary_terrain_material.py")) == {"A"},
          "M_TemporaryTerrain reads only the alpha (the rock) of its vertex color")
    check(vertex_color_reads(read("terrain_edit_light_nodes.py")) == {"B"},
          "the light floor reads the blue of the vertex color")
    check("add_edit_light(material, shade)" in read("create_temporary_terrain_material.py"),
          "M_TemporaryTerrain adds the light floor to its base color")
    for name in sorted(os.listdir(scripts)):
        if not name.endswith(".py"):
            continue
        text = read(name)
        if name.startswith(("create_water", "water_")):
            check("VertexColor" not in text, f"{name} reads no vertex color")
        if name.startswith("temporary_terrain") or name.endswith("_hlsl.py"):
            check("VertexColor" not in text, f"{name} reads no vertex color")


def main():
    scripts, private = sys.argv[1], sys.argv[2]
    check_nodes(scripts, private)
    check_readers(scripts)
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("the light floor of the edits in the scripts: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
