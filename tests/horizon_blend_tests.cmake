# The tests of the surface blend of the far horizon (agent 29: HorizonCell::blend, the weights of
# the 2 or 3 dominant biomes and materials of its samples, and the GPU horizon that blends them).
# The tests of a task live in a file of their own, included once from tests/CMakeLists.txt.

# The time of the samples of each level and the memory of a sample: a bench run by hand,
# horizon_blend_cost_bench (tests/guard_tests.cmake).

# The blend of the samples: the texels of the tiles on level 0 (with and without the planet), the
# point blends below HorizonParams::blendFromLevel, the footprint means above, the sums of 255,
# the rectangles and the threads, the edits.
terrain_add_test(horizon_blend_test terrain_core)
target_include_directories(horizon_blend_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/streaming)

# The blend texels of the GPU horizon of the Unreal plugin: channels, regions, written with the
# texels of their samples level by level and after a teleport.
terrain_add_test(unreal_horizon_blend_test terrain_unreal_bridge)

# The HLSL of the blends in the materials of the horizon and of the tiles (identifiers, scopes,
# inputs, unfiltered reads) and a reference of its formulas: weights summing to 1 through the morph.
find_package(Python COMPONENTS Interpreter QUIET)
if(Python_Interpreter_FOUND)
    add_test(NAME unreal_horizon_blend_scripts_test
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unreal_horizon_blend_scripts_test.py
                ${PROJECT_SOURCE_DIR}/terrain_unreal)
endif()
