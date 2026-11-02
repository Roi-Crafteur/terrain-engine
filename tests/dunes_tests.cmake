# The seas of dunes (agent 34, ergs.hpp): the forms of the dunes, the mask of the ergs from the
# planet level, the world with and without them. Included once from tests/CMakeLists.txt.
terrain_add_test(dune_forms_test terrain_core)
target_include_directories(dune_forms_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(dunes_shape_test terrain_core)
terrain_add_test(ergs_mask_test terrain_core)
terrain_add_test(ergs_world_test terrain_core)
