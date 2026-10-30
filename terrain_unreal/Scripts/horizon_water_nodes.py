"""The nodes of the lakes and rivers of the GPU horizon (agent 27), for
create_horizon_gpu_material.py: its water parameters, the vertex node that raises the water to its
level (HorizonWaterVertex), the pixel node that draws it over the ground (HorizonInlandWater), the
world position offset and the opacity mask they change. g: the module of the material script, for
its helpers (expression, connect, custom_node, texture_parameter, local_position, world_position,
mask, interpolate). The code: horizon_water_hlsl.py and water_look_hlsl.py.
"""

import importlib

import unreal

import horizon_water_hlsl as code
import water_look_hlsl as look
code = importlib.reload(code)  # the modules of this checkout, even if run before
look = importlib.reload(look)

FLOAT1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
FLOAT3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
VERTEX_INPUTS = ["Tex", "Coarse", "Levels", "CoarseLevels", "Local", "Kind", "Viewer"]
VERTEX_SCALARS = ["Cells", "FirstSlotX", "FirstSlotZ", "CoarseX", "CoarseZ", "CoarseFirstX",
                  "CoarseFirstZ", "HasCoarser", "MorphCells", "PitchCm", "DropCoveredCm",
                  "InlandWater"]
VERTEX_OUTPUTS = ["InlandDepthOut", "InlandOpenOut", "InlandKindOut", "InlandAltitudeOut"]
PIXEL_OUTPUTS = ["EmissiveOut", "NormalOut", "RoughnessOut", "SpecularOut", "KeepOut"]
PIXEL_INPUTS = ["Color", "Emissive", "Normal", "Roughness", "Specular", "Depth", "Open",
                "KindCode", "Altitude", "CameraRel", "Time", "SunDir", "SunIlluminance", "SkyView",
                "PlanetRadiusCm", "WaveOriginX", "WaveOriginY"]


def parameters(material, g):
    """The water textures of the level and of the coarser one, InlandWater (1: drawn) and the look
    of the water (the parameters of M_TerrainWater, the same defaults)."""
    nodes = {"HorizonWater": g.texture_parameter(material, "HorizonWater", -2200),
             "CoarseWater": g.texture_parameter(material, "CoarseWater", -2050)}
    inland = g.expression(material, unreal.MaterialExpressionScalarParameter, -1300, -2400)
    inland.set_editor_property("parameter_name", "InlandWater")
    inland.set_editor_property("default_value", 1.0)
    nodes["InlandWater"] = inland
    y = -3400
    for name, value in look.WATER_COLORS.items():
        node = g.expression(material, unreal.MaterialExpressionVectorParameter, -1600, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
        nodes[name] = node
        y += 120
    for name, value in look.WATER_SCALARS.items():
        node = g.expression(material, unreal.MaterialExpressionScalarParameter, -1600, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        nodes[name] = node
        y += 60
    return nodes


def vertex(material, g, nodes, kind):
    """HorizonWaterVertex: how far the vertex rises to the water, and what the pixels need."""
    custom = g.custom_node(
        material, -900, -2600, "HorizonWaterVertex", code.WATER_VERTEX_HLSL,
        VERTEX_INPUTS + VERTEX_SCALARS,
        [(name, FLOAT1) for name in VERTEX_OUTPUTS], FLOAT3)
    for source, name in (("HorizonTexels", "Tex"), ("CoarseTexels", "Coarse"),
                         ("HorizonWater", "Levels"), ("CoarseWater", "CoarseLevels")):
        g.connect(nodes[source], "", custom, name)
    g.connect(g.expression(material, unreal.MaterialExpressionPreSkinnedPosition, -1600, -2600),
              "", custom, "Local")
    g.connect(kind, "", custom, "Kind")
    viewer = g.local_position(material, -1600, -2500)
    g.connect(g.expression(material, unreal.MaterialExpressionCameraPositionWS, -1800, -2500), "",
              viewer, "")
    g.connect(g.mask(material, viewer, -1400, -2500), "", custom, "Viewer")
    for name in VERTEX_SCALARS:
        g.connect(nodes[name], "", custom, name)
    return custom


def world_offset(material, g, ground, water):
    """The offset of the ground (HorizonVertex) and the rise to the water."""
    add = g.expression(material, unreal.MaterialExpressionAdd, 600, 200)
    g.connect(ground, "", add, "A")
    g.connect(water, "", add, "B")
    return add


def pixel(material, g, nodes, shade, water):
    """HorizonInlandWater: the water over the shaded ground (HorizonShade) of the pixel."""
    sky_view = unreal.MaterialExpressionSkyAtmosphereViewLuminance
    custom = g.custom_node(
        material, 600, -300, "HorizonInlandWater",
        look.struct_hlsl() + code.inland_hlsl(look.SKY_HLSL, look.BODY_CALL),
        PIXEL_INPUTS + look.look_inputs(),
        [("EmissiveOut", FLOAT3), ("NormalOut", FLOAT3), ("RoughnessOut", FLOAT1),
         ("SpecularOut", FLOAT1), ("KeepOut", FLOAT1)], FLOAT3)
    for output, name in (("", "Color"), ("EmissiveOut", "Emissive"), ("NormalOut", "Normal"),
                         ("RoughnessOut", "Roughness"), ("SpecularOut", "Specular")):
        g.connect(shade, output, custom, name)
    for index, (output, name) in enumerate((("InlandDepthOut", "Depth"), ("InlandOpenOut", "Open"),
                                            ("InlandKindOut", "KindCode"),
                                            ("InlandAltitudeOut", "Altitude"))):
        g.connect(g.interpolate(material, water, output, -2900 + 100 * index), "", custom, name)
    g.connect(g.world_position(material, unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE,
                               300, -1500), "", custom, "CameraRel")
    for cls, name, y in ((unreal.MaterialExpressionTime, "Time", -1600),
                         (unreal.MaterialExpressionSkyAtmosphereLightDirection, "SunDir", -1700),
                         (unreal.MaterialExpressionSkyAtmosphereLightIlluminance, "SunIlluminance",
                          -1800),
                         (sky_view, "SkyView", -1900)):  # SkyView binds the LUT the code samples
        g.connect(g.expression(material, cls, 300, y), "", custom, name)
    for name in ["PlanetRadiusCm", "WaveOriginX", "WaveOriginY"] + look.look_inputs():
        g.connect(nodes[name], "", custom, name)
    return custom


def opacity_mask(material, g, square, inland, attributes):
    """The sea square cut where the core puts no ocean, and the cells of water across the edge of
    the tiles."""
    keep = g.expression(material, unreal.MaterialExpressionMultiply, 650, -500)
    g.connect(square, "", keep, "A")
    g.connect(inland, "KeepOut", keep, "B")
    g.connect(keep, "", attributes, "OpacityMask")
