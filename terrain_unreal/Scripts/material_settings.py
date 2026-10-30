"""The settings of a material rebuilt in place (agent 27), for the scripts of this folder.

A script that rebuilds a material deletes its expressions (temporary_terrain_look.clear_material)
but the asset keeps its settings: a blend mode, a shading model or material attributes left by an
older version of the script (or by hand) would silently change what the new graph draws (the
direct connections of a material go nowhere once it uses material attributes). reset_material sets
every setting the scripts may change back to the value of a new material, then the wanted ones.
A setting this version of Unreal does not have is reported and skipped.
"""

import unreal

# The value of each setting in a new material (UMaterial defaults, UE 5.8).
DEFAULTS = {
    "material_domain": unreal.MaterialDomain.MD_SURFACE,
    "blend_mode": unreal.BlendMode.BLEND_OPAQUE,
    "shading_model": unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
    "use_material_attributes": False,
    "two_sided": False,
    "tangent_space_normal": True,
    "opacity_mask_clip_value": 0.3333,
    "dithered_lod_transition": False,
    "fully_rough": False,
    "cast_shadow_as_masked": False,
    "translucency_lighting_mode":
        unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL,
    "num_customized_uvs": 0,
    "is_sky": False,
    "disable_depth_test": False,
}


def reset_material(material, **settings):
    """Every setting of DEFAULTS back to its default, then settings (name=value) on top."""
    wanted = dict(DEFAULTS)
    wanted.update(settings)
    for name, value in wanted.items():
        try:
            material.set_editor_property(name, value)
        except Exception as error:  # noqa: BLE001  (a setting this Unreal does not have)
            unreal.log_warning(f"{material.get_name()}: setting {name} not reset ({error})")
