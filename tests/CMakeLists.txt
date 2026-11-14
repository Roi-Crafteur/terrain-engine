# Each test is a plain executable registered with CTest; it fails by returning non-zero.
# Adopting a test framework later only requires changing terrain_add_test and test_assert.hpp.

# Builds the tests, then runs them: cmake --build <build-dir> --target check
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure "$<$<BOOL:$<CONFIG>>:-C;$<CONFIG>>"
    COMMAND_EXPAND_LISTS
    USES_TERMINAL
)

function(terrain_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN} terrain_warnings)
    add_test(NAME ${name} COMMAND ${name})
    add_dependencies(check ${name})
endfunction()

terrain_add_test(test_chunk_coord terrain_core)
terrain_add_test(det_math_test terrain_core)
terrain_add_test(det_math_inline_test terrain_core)
terrain_add_test(world_size_test terrain_core)
terrain_add_test(test_camera_math terrain_viewer_camera)
terrain_add_test(test_cave_look terrain_viewer_cave_look)
terrain_add_test(test_lava_look terrain_viewer_cave_look)
terrain_add_test(test_view_modes terrain_viewer_modes)
terrain_add_test(marching_cubes_test terrain_core)
terrain_add_test(noise_test terrain_core)
terrain_add_test(noise_periodic_test terrain_core)
terrain_add_test(noise_octaves_test terrain_core)
terrain_add_test(noise_slope_test terrain_core)
terrain_add_test(heightfield_test terrain_core)
terrain_add_test(heightfield_shortcuts_test terrain_core)
terrain_add_test(heightfield_batch_test terrain_core)
terrain_add_test(heightfield_macro_test terrain_core)
terrain_add_test(heightfield_voronoi_test terrain_core)
terrain_add_test(heightfield_density_test terrain_core)
terrain_add_test(png_test terrain_core)
terrain_add_test(heightmap_png_test terrain_core)
terrain_add_test(erosion_test terrain_core)
terrain_add_test(rivers_test terrain_core)
terrain_add_test(eroded_region_test terrain_core)
terrain_add_test(eroded_default_world_test terrain_core)
terrain_add_test(eroded_png_test terrain_core)
terrain_add_test(sdf_test terrain_core)
terrain_add_test(cliffs_test terrain_core)
terrain_add_test(cliff_region_test terrain_core)
terrain_add_test(cliff_rivers_test terrain_core)
terrain_add_test(cliffs_png_test terrain_core)
terrain_add_test(overhangs_test terrain_core)
terrain_add_test(arches_test terrain_core)
terrain_add_test(sculpted_region_test terrain_core)
terrain_add_test(cliffs_default_world_test terrain_core)
terrain_add_test(region_wrap_test terrain_core)
terrain_add_test(world_wrap_streaming_test terrain_core)
terrain_add_test(world_seams_noise_test terrain_core)
terrain_add_test(world_seams_caves_test terrain_core)
terrain_add_test(world_seams_objects_test terrain_core)
terrain_add_test(sculpted_water_test terrain_core)
terrain_add_test(arches_png_test terrain_core)
terrain_add_test(terrain_surface_test terrain_core)
terrain_add_test(climate_test terrain_core)
terrain_add_test(biome_test terrain_core)
terrain_add_test(surface_material_test terrain_core)
terrain_add_test(vegetation_test terrain_core)
terrain_add_test(ecology_test terrain_core)
terrain_add_test(ecology_default_world_test terrain_core)
terrain_add_test(ecology_cliffs_test terrain_core)
terrain_add_test(ecology_png_test terrain_core)
terrain_add_test(biome_blend_test terrain_core)
target_include_directories(biome_blend_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(biome_blend_world_test terrain_core)
terrain_add_test(blend_png_test terrain_core)
terrain_add_test(voronoi_layout_test terrain_core)
terrain_add_test(voronoi_test terrain_core)
terrain_add_test(voronoi_boundaries_test terrain_core)
terrain_add_test(voronoi_png_test terrain_core)
terrain_add_test(lod_test terrain_core)
terrain_add_test(lod_seams_test terrain_core)
include(${CMAKE_CURRENT_LIST_DIR}/lod_seams_tests.cmake)
terrain_add_test(stream_manager_test terrain_core)
terrain_add_test(modifications_test terrain_core)
terrain_add_test(block_section_test terrain_core)
terrain_add_test(volume_edit_test terrain_core)
terrain_add_test(edit_window_test terrain_core)
terrain_add_test(edit_summary_test terrain_core)
terrain_add_test(modification_sizes_test terrain_core)
# The compressed masks of the sections (internal header).
terrain_add_test(section_codec_test terrain_core)
target_include_directories(section_codec_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/storage)
# The frames read in pieces (internal header).
terrain_add_test(frame_reader_test terrain_core)
target_include_directories(frame_reader_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/storage)
terrain_add_test(terrain_tiles_test terrain_core)
terrain_add_test(tile_blend_test terrain_core)
target_include_directories(tile_blend_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/streaming)
terrain_add_test(streaming_default_world_test terrain_core)
terrain_add_test(horizon_levels_test terrain_core)
terrain_add_test(horizon_sampler_test terrain_core)
terrain_add_test(horizon_crests_test terrain_core)
terrain_add_test(horizon_normals_test terrain_core)
terrain_add_test(horizon_seams_test terrain_core)
terrain_add_test(horizon_grids_test terrain_core)
terrain_add_test(horizon_streamer_test terrain_core)
terrain_add_test(horizon_png_test terrain_core)
terrain_add_test(persistence_test terrain_core)
terrain_add_test(edit_tiles_test terrain_core)
terrain_add_test(world_meta_test terrain_core)
terrain_add_test(region_file_test terrain_core)
terrain_add_test(world_save_test terrain_core)
terrain_add_test(world_migration_test terrain_core)
terrain_add_test(world_directory_test terrain_core)
terrain_add_test(world_volume_test terrain_core)
terrain_add_test(edit_tiles_golden_test terrain_core)
terrain_add_test(caves_test terrain_core)
terrain_add_test(caves_default_world_test terrain_core)
terrain_add_test(cave_entrances_test terrain_core)
terrain_add_test(cave_region_test terrain_core)
terrain_add_test(cave_tiles_test terrain_core)
terrain_add_test(cave_seams_test terrain_core)
terrain_add_test(cave_districts_carving_test terrain_core)
terrain_add_test(cave_districts_borders_test terrain_core)
terrain_add_test(cave_districts_water_test terrain_core)
terrain_add_test(cave_districts_lakes_test terrain_core)
terrain_add_test(cave_districts_speleothems_test terrain_core)
terrain_add_test(cave_districts_tiles_test terrain_core)
terrain_add_test(cave_districts_seams_test terrain_core)
terrain_add_test(caves_png_test terrain_core)
terrain_add_test(caves_png_lakes_test terrain_core)
terrain_add_test(cave_regions_png_test terrain_core)
terrain_add_test(cave_features_png_test terrain_core)
terrain_add_test(cave_features_meeting_test terrain_core)
terrain_add_test(aquifers_test terrain_core)
terrain_add_test(speleothems_test terrain_core)
terrain_add_test(cave_materials_test terrain_core)
terrain_add_test(cave_materials_regions_test terrain_core)
terrain_add_test(cave_golden_test terrain_core)
terrain_add_test(heightfield_golden_test terrain_core)
terrain_add_test(world_golden_test terrain_core)
terrain_add_test(horizon_golden_test terrain_core)
terrain_add_test(blend_golden_test terrain_core)
terrain_add_test(golden_ftz_test terrain_core)

# Frozen reference of the noise, the Heightfield, the plates and the cave noise nodes (commit
# 052078b, before the noise performance work) and of the meshing (commit 7cf4877, before the
# meshing performance work): frozen_reference_test and marching_cubes_frozen_test compare the live
# code with it bit for bit, beyond the few thousand points of the golden checksums.
add_library(terrain_frozen_reference STATIC
    frozen/frozen_heightfield.cpp
    frozen/frozen_marching_cubes.cpp
    frozen/frozen_noise.cpp
    frozen/frozen_plates.cpp
    frozen/frozen_surface.cpp
)
target_include_directories(terrain_frozen_reference PUBLIC frozen)
# The Marching Cubes tables, shared with the live meshing.
target_include_directories(terrain_frozen_reference PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/meshing)
target_link_libraries(terrain_frozen_reference PUBLIC terrain_core PRIVATE terrain_warnings)
terrain_add_test(frozen_reference_test terrain_frozen_reference)
terrain_add_test(marching_cubes_frozen_test terrain_frozen_reference)
terrain_add_test(height_access_test terrain_frozen_reference)
terrain_add_test(cave_districts_test terrain_core)
terrain_add_test(cave_district_weights_test terrain_core)
terrain_add_test(cave_districts_neutral_test terrain_core)
terrain_add_test(cave_flora_test terrain_core)
terrain_add_test(cave_galleries_test terrain_core)
terrain_add_test(cave_galleries_network_test terrain_core)
terrain_add_test(cave_rivers_test terrain_core)
terrain_add_test(cave_rivers_water_test terrain_core)
terrain_add_test(cave_rivers_tiles_test terrain_core)
terrain_add_test(cave_lava_test terrain_core)
terrain_add_test(cave_lava_tiles_test terrain_core)
terrain_add_test(cave_fluid_objects_test terrain_core)

# Planet level (phase 11A): climate, hydrology and river graph on a grid of the whole world.
terrain_add_test(planet_grid_test terrain_core)
terrain_add_test(planet_climate_test terrain_core)
terrain_add_test(planet_hierarchy_test terrain_core)
terrain_add_test(planet_lakes_test terrain_core)
terrain_add_test(planet_flow_test terrain_core)
terrain_add_test(planet_rivers_test terrain_core)
terrain_add_test(planet_features_test terrain_core)
terrain_add_test(planet_breach_test terrain_core)
terrain_add_test(planet_level_test terrain_core)
terrain_add_test(planet_file_test terrain_core)
terrain_add_test(hydrology_test terrain_core)
terrain_add_test(water_cost_test terrain_core)
# A measure of time: alone, even under ctest -j.
set_tests_properties(water_cost_test PROPERTIES RUN_SERIAL TRUE)
include(${CMAKE_CURRENT_SOURCE_DIR}/cost_ratio_tests.cmake) # agent 30: stable cost ratios
terrain_add_test(river_falls_test terrain_core)
terrain_add_test(planet_compact_test terrain_core)
terrain_add_test(planet_golden_test terrain_core)
terrain_add_test(planet_stats_test terrain_core)
terrain_add_test(relief_lakes_test terrain_core)
terrain_add_test(planet_false_passes_test terrain_core)
terrain_add_test(planet_png_test terrain_core)
include(${CMAKE_CURRENT_SOURCE_DIR}/planet_memory_tests.cmake)

# The grid primitives of the planetary geography (internal header).
terrain_add_test(geography_grid_test terrain_core)
target_include_directories(geography_grid_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(geography_test terrain_core)
terrain_add_test(geography_relief_test terrain_core)
terrain_add_test(ravines_test terrain_core)
target_include_directories(ravines_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(mountain_forms_test terrain_core)
target_include_directories(mountain_forms_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(mountain_walls_test terrain_core)
target_include_directories(mountain_walls_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(mountain_freeze_test terrain_core)
terrain_add_test(depressions_test terrain_core)
terrain_add_test(plain_forms_test terrain_core)
target_include_directories(plain_forms_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(geography_tectonics_test terrain_core)
target_include_directories(geography_tectonics_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(relief_png_test terrain_core)
terrain_add_test(relief_stats_test terrain_core)
terrain_add_test(relief_view_test terrain_core)

include(${CMAKE_CURRENT_SOURCE_DIR}/relief_tools_tests.cmake) # the tools of the relief and the water

# No platform-dependent floating-point function in the sources of terrain_core (std::sin,
# std::pow, std::fma, long double...): terrain::dmath replaces them.
add_test(NAME forbidden_math_test
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/terrain_core
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/forbidden_math_test.cmake)

# Pure C++ layer of the Unreal plugin (terrain_unreal/Source/TerrainEngine/Private/Bridge): no
# Unreal header, compiled and tested here. The plugin itself is only ever built by Unreal.
set(unreal_bridge ${PROJECT_SOURCE_DIR}/terrain_unreal/Source/TerrainEngine/Private/Bridge)
add_library(terrain_unreal_bridge STATIC
    ${unreal_bridge}/altitude_color.cpp
    ${unreal_bridge}/determinism_check.cpp
    ${unreal_bridge}/edit_thread.cpp
    ${unreal_bridge}/horizon_atmosphere.cpp
    ${unreal_bridge}/horizon_blend_texels.cpp
    ${unreal_bridge}/horizon_epochs.cpp
    ${unreal_bridge}/horizon_gpu_streamer.cpp
    ${unreal_bridge}/horizon_layout.cpp
    ${unreal_bridge}/horizon_mesh.cpp
    ${unreal_bridge}/horizon_normal_stream.cpp
    ${unreal_bridge}/horizon_normal_texels.cpp
    ${unreal_bridge}/horizon_morph.cpp
    ${unreal_bridge}/horizon_streamer.cpp
    ${unreal_bridge}/horizon_textures.cpp
    ${unreal_bridge}/horizon_timings.cpp
    ${unreal_bridge}/terrain_edits.cpp
    ${unreal_bridge}/terrain_session.cpp
    ${unreal_bridge}/terrain_surface.cpp
    ${unreal_bridge}/tile_blend_atlas.cpp
    ${unreal_bridge}/tile_mesh_conversion.cpp
    ${unreal_bridge}/tile_streamer.cpp
    ${unreal_bridge}/tile_timings.cpp
    ${unreal_bridge}/unreal_frame.cpp
)
target_include_directories(terrain_unreal_bridge PUBLIC ${unreal_bridge})
target_link_libraries(terrain_unreal_bridge PUBLIC terrain_core PRIVATE terrain_warnings)

terrain_add_test(unreal_frame_test terrain_unreal_bridge)
terrain_add_test(unreal_tile_mesh_test terrain_unreal_bridge)
terrain_add_test(unreal_altitude_color_test terrain_unreal_bridge)
terrain_add_test(unreal_tile_timings_test terrain_unreal_bridge)
terrain_add_test(unreal_tile_streamer_test terrain_unreal_bridge)
terrain_add_test(unreal_terrain_session_test terrain_unreal_bridge)
terrain_add_test(unreal_determinism_check_test terrain_unreal_bridge)
terrain_add_test(unreal_horizon_layout_test terrain_unreal_bridge)
terrain_add_test(unreal_horizon_mesh_test terrain_unreal_bridge)
terrain_add_test(unreal_horizon_streamer_test terrain_unreal_bridge)
terrain_add_test(unreal_horizon_epochs_test terrain_unreal_bridge)
terrain_add_test(unreal_horizon_textures_test terrain_unreal_bridge)
terrain_add_test(unreal_horizon_gpu_streamer_test terrain_unreal_bridge)
terrain_add_test(unreal_horizon_normals_test terrain_unreal_bridge)
terrain_add_test(unreal_horizon_atmosphere_test terrain_unreal_bridge)
terrain_add_test(unreal_terrain_surface_test terrain_unreal_bridge)
terrain_add_test(unreal_tile_blend_atlas_test terrain_unreal_bridge)
include(horizon_detail_tests.cmake)

# The player edits seen from the far horizon (phases 13 and 15).
include(${CMAKE_CURRENT_SOURCE_DIR}/horizon_edits_tests.cmake)

# Every source of terrain_core outside io/ has its wrapper in the Unreal plugin, which compiles
# it: a source added to the core without its wrapper fails here.
add_test(NAME unreal_core_sources
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/unreal_core_sources.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/export_tools_tests.cmake) # the export tools of the first phases
include(${CMAKE_CURRENT_SOURCE_DIR}/water_level_tests.cmake) # task 7b: the water level
include(rock_tests.cmake) # the rock of the streamed world (phase 12A)
include(${CMAKE_CURRENT_SOURCE_DIR}/settlement_tests.cmake) # agent 25: the settlement V1
include(${CMAKE_CURRENT_SOURCE_DIR}/unreal_water_tests.cmake) # agent 27: the water in Unreal
include(${CMAKE_CURRENT_SOURCE_DIR}/coast_tests.cmake) # agent 33: the coastal relief
include(${CMAKE_CURRENT_SOURCE_DIR}/loading_tests.cmake) # agent 38: the load at the spawn and after a teleport
include(${CMAKE_CURRENT_SOURCE_DIR}/dev_tools_tests.cmake) # agent 36: the tools in the game
include(${CMAKE_CURRENT_SOURCE_DIR}/verify_tests.cmake) # agent 37: the probe and the verification
include(${CMAKE_CURRENT_SOURCE_DIR}/dev_map_tests.cmake) # agent 35: the dev map
include(${CMAKE_CURRENT_SOURCE_DIR}/horizon_blend_tests.cmake) # agent 29: the biome weights of the horizon
include(${CMAKE_CURRENT_SOURCE_DIR}/break_place_tests.cmake) # agent 28: break and place in game
include(${CMAKE_CURRENT_SOURCE_DIR}/dunes_tests.cmake) # agent 34: the seas of dunes
include(${CMAKE_CURRENT_SOURCE_DIR}/biomes_tests.cmake) # agent 49: the biomes of the land
include(${CMAKE_CURRENT_SOURCE_DIR}/guard_tests.cmake) # agent 42: the guards of the goldens and of the tests
include(${CMAKE_CURRENT_SOURCE_DIR}/serial_tests.cmake) # agent 42: the tests that run alone, after the tests it names
