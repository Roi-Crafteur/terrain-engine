"""The clock of the water (agent 40). Every material of the water reads its time through one
Custom node, WaterClock: the Time of the engine while the parameter WaterTime is below 0 (its
default), else WaterTime itself, frozen. Terrain.Param WaterTime 10 freezes the waves, the ripples,
the foam, the curtains of the falls and the dither of the water, on the tiles and on the far
horizon, so that the two images of an A/B differ only by the switch compared; Terrain.Param
WaterTime -1 lets the water run again. Nothing of the map changes.

Used by create_water_materials.py (the water and the falls of the tiles) and horizon_water_nodes.py
(the lakes and rivers of the horizon, and its sea for create_horizon_gpu_material.py).
"""

import unreal

# Below 0: the time of the engine; from 0 on: that time, frozen (seconds).
WATER_TIME_DEFAULT = -1.0
CLOCK_INPUTS = ["Time", "WaterTime"]
CLOCK_HLSL = "return WaterTime < 0.0 ? Time : WaterTime;"


def node(material, x, y):
    """The time of the water at (x, y) of the graph, its inputs wired: connect its output where
    the code reads Time."""
    editing = unreal.MaterialEditingLibrary
    clock = editing.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    clock.set_editor_property("code", CLOCK_HLSL)
    clock.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    clock.set_editor_property("description", "WaterClock")
    inputs = []
    for name in CLOCK_INPUTS:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    clock.set_editor_property("inputs", inputs)
    time = editing.create_material_expression(material, unreal.MaterialExpressionTime, x - 250, y)
    frozen = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter,
                                                x - 250, y + 60)
    frozen.set_editor_property("parameter_name", "WaterTime")
    frozen.set_editor_property("default_value", WATER_TIME_DEFAULT)
    for source, name in ((time, "Time"), (frozen, "WaterTime")):
        if not editing.connect_material_expressions(source, "", clock, name):
            raise RuntimeError(f"cannot connect {name} to the clock of the water")
    return clock
