# The tests of the surface blend of the far horizon (agent 29: HorizonCell::blend, the weights of
# the 2 or 3 dominant biomes and materials of its samples, and the GPU horizon that blends them).
# The tests of a task live in a file of their own, included once from tests/CMakeLists.txt.

# The time of the samples of each level and the memory of a sample, printed only.
terrain_add_test(horizon_blend_cost_test terrain_core)

# The blend of the samples: the texels of the tiles on level 0 (with and without the planet), the
# point blends below HorizonParams::blendFromLevel, the footprint means above, the sums of 255,
# the rectangles and the threads, the edits.
terrain_add_test(horizon_blend_test terrain_core)
target_include_directories(horizon_blend_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/streaming)
