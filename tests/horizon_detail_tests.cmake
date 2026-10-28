# The detail of the far horizon computed after its samples: the fine grid shared by the detail
# normals and the crests (agent 21). Included by tests/CMakeLists.txt after the bridge library.
terrain_add_test(horizon_fine_grid_test terrain_core)
terrain_add_test(horizon_detail_test terrain_core)
terrain_add_test(horizon_detail_water_test terrain_core)

# The crests of the GPU horizon computed after the epochs, in the bridge.
target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/horizon_crest_texels.cpp)
terrain_add_test(unreal_horizon_detail_test terrain_unreal_bridge)
terrain_add_test(unreal_horizon_crests_test terrain_unreal_bridge)

# Measures of the GPU horizon, run by hand (not a test): see the header of the source.
add_executable(unreal_horizon_bench unreal_horizon_bench.cpp)
target_link_libraries(unreal_horizon_bench PRIVATE terrain_unreal_bridge terrain_warnings)
