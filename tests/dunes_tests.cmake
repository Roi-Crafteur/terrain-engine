# The seas of dunes (agent 34, ergs.hpp): the forms of the dunes, the mask of the ergs from the
# planet level, the world with and without them. Included once from tests/CMakeLists.txt.
terrain_add_test(dune_forms_test terrain_core)
target_include_directories(dune_forms_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(dunes_shape_test terrain_core)
terrain_add_test(ergs_mask_test terrain_core)
terrain_add_test(ergs_world_test terrain_core)
terrain_add_test(ergs_horizon_test terrain_core)
terrain_add_test(dunes_blend_test terrain_core)
target_include_directories(dunes_blend_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/streaming)
terrain_add_test(ergs_shore_test terrain_core)
terrain_add_test(dunes_waves_test terrain_core)
target_include_directories(dunes_waves_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(ergs_characters_test terrain_core)
terrain_add_test(dunes_far_test terrain_core)
terrain_add_test(dunes_mesh_test terrain_core)
terrain_add_test(dunes_seam_test terrain_core)
terrain_add_test(dunes_crescents_test terrain_core)
find_package(Python COMPONENTS Interpreter QUIET)
if(Python_Interpreter_FOUND)
    add_test(NAME unreal_dune_ripples_test
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unreal_dune_ripples_test.py
                ${PROJECT_SOURCE_DIR}/terrain_unreal/Scripts)
    add_test(NAME unreal_erg_rock_test
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unreal_erg_rock_test.py
                ${PROJECT_SOURCE_DIR}/terrain_unreal/Scripts
                ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
endif()
