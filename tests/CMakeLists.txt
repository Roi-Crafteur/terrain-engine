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
terrain_add_test(world_size_test terrain_core)
terrain_add_test(test_camera_math terrain_viewer_camera)
terrain_add_test(test_cave_look terrain_viewer_cave_look)
terrain_add_test(test_lava_look terrain_viewer_cave_look)
terrain_add_test(test_view_modes terrain_viewer_modes)
terrain_add_test(marching_cubes_test terrain_core)
terrain_add_test(noise_test terrain_core)
terrain_add_test(noise_periodic_test terrain_core)
terrain_add_test(heightfield_test terrain_core)
terrain_add_test(heightfield_macro_test terrain_core)
terrain_add_test(heightfield_voronoi_test terrain_core)
terrain_add_test(heightfield_density_test terrain_core)
terrain_add_test(png_test terrain_core)
terrain_add_test(heightmap_png_test terrain_core)
terrain_add_test(erosion_test terrain_core)
terrain_add_test(rivers_test terrain_core)
terrain_add_test(eroded_region_test terrain_core)
terrain_add_test(eroded_png_test terrain_core)
terrain_add_test(sdf_test terrain_core)
terrain_add_test(cliffs_test terrain_core)
terrain_add_test(cliff_region_test terrain_core)
terrain_add_test(cliff_rivers_test terrain_core)
terrain_add_test(cliffs_png_test terrain_core)
terrain_add_test(overhangs_test terrain_core)
terrain_add_test(arches_test terrain_core)
terrain_add_test(sculpted_region_test terrain_core)
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
terrain_add_test(ecology_cliffs_test terrain_core)
terrain_add_test(ecology_png_test terrain_core)
terrain_add_test(voronoi_layout_test terrain_core)
terrain_add_test(voronoi_test terrain_core)
terrain_add_test(voronoi_boundaries_test terrain_core)
terrain_add_test(voronoi_png_test terrain_core)
terrain_add_test(lod_test terrain_core)
terrain_add_test(lod_seams_test terrain_core)
terrain_add_test(stream_manager_test terrain_core)
terrain_add_test(modifications_test terrain_core)
terrain_add_test(terrain_tiles_test terrain_core)
terrain_add_test(horizon_levels_test terrain_core)
terrain_add_test(horizon_sampler_test terrain_core)
terrain_add_test(horizon_seams_test terrain_core)
terrain_add_test(horizon_grids_test terrain_core)
terrain_add_test(horizon_streamer_test terrain_core)
terrain_add_test(horizon_cost_test terrain_core)
terrain_add_test(horizon_png_test terrain_core)
terrain_add_test(persistence_test terrain_core)
terrain_add_test(world_meta_test terrain_core)
terrain_add_test(region_file_test terrain_core)
terrain_add_test(world_save_test terrain_core)
terrain_add_test(world_migration_test terrain_core)
terrain_add_test(world_directory_test terrain_core)
terrain_add_test(world_volume_test terrain_core)
terrain_add_test(caves_test terrain_core)
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
terrain_add_test(golden_ftz_test terrain_core)
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
    ${unreal_bridge}/terrain_session.cpp
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

# Every source of terrain_core outside io/ has its wrapper in the Unreal plugin, which compiles
# it: a source added to the core without its wrapper fails here.
add_test(NAME unreal_core_sources
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/unreal_core_sources.cmake)

# Smoke test: writes heightmap.png and heightmap_preview.png (1024 x 1024) into
# <build-dir>/heightmap for a visual check.
add_test(NAME export_heightmap_smoke
    COMMAND terrain_export_heightmap ${CMAKE_BINARY_DIR}/heightmap)
add_dependencies(check terrain_export_heightmap)

# Smoke test of the region arguments: 64 x 64 px at 250 m/px around (5000, -3000), written
# into <build-dir>/heightmap_args. The summary is printed only once the images are written.
add_test(NAME export_heightmap_args_smoke
    COMMAND terrain_export_heightmap ${CMAKE_BINARY_DIR}/heightmap_args 20261019 250 64 5000
            -3000)
set_tests_properties(export_heightmap_args_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "64 x 64 px, 16\\.0 km wide, center \\(5000, -3000\\)")

# Smoke test of the macro continent argument: the same region with an amplitude of 0.5, shown
# in the summary. Written into <build-dir>/heightmap_macro_args.
add_test(NAME export_heightmap_macro_smoke
    COMMAND terrain_export_heightmap ${CMAKE_BINARY_DIR}/heightmap_macro_args 20261019 250 64
            5000 -3000 0.5)
set_tests_properties(export_heightmap_macro_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION
    "64 x 64 px, 16\\.0 km wide, center \\(5000, -3000\\), macro continent 0\\.50")

# Invalid arguments (size 1, non-numeric center, negative macro continent, ...) are rejected
# with an error message, before anything is written.
add_test(NAME export_heightmap_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_heightmap>
            -DOUT=${CMAKE_BINARY_DIR}/heightmap_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_heightmap_bad_args.cmake)

# Smoke test: writes heightmap_eroded.png, heightmap_eroded_preview.png and rivers.png
# (512 x 512, 100 m/px) into <build-dir>/eroded for a visual check.
add_test(NAME export_eroded_smoke
    COMMAND terrain_export_eroded ${CMAKE_BINARY_DIR}/eroded)
add_dependencies(check terrain_export_eroded)

# Smoke test: writes cliffs_mask.png and cliffs_preview.png (512 x 512, 12.5 m/px, around the
# cliff site of the default world) into <build-dir>/cliffs for a visual check.
add_test(NAME export_cliffs_smoke
    COMMAND terrain_export_cliffs ${CMAKE_BINARY_DIR}/cliffs)
add_dependencies(check terrain_export_cliffs)

# Smoke test: writes overhangs_mask.png and arches_preview.png (512 x 512, 12.5 m/px, the
# region of the cliff export) into <build-dir>/arches for a visual check.
add_test(NAME export_arches_smoke
    COMMAND terrain_export_arches ${CMAKE_BINARY_DIR}/arches)
add_dependencies(check terrain_export_arches)

# Smoke test: writes biome_map.png, surface_material_map.png and vegetation_density_map.png
# (512 x 512, 200 m/px, an eroded region around the origin) into <build-dir>/biomes for a visual
# check. The summary lists the biomes present, printed once the images are written.
add_test(NAME export_biomes_smoke
    COMMAND terrain_export_biomes ${CMAKE_BINARY_DIR}/biomes)
set_tests_properties(export_biomes_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "512 x 512 px, 102\\.4 km wide.*[0-9]+ biomes: ocean")
add_dependencies(check terrain_export_biomes)

# Smoke test: writes voronoi_plates.png, voronoi_heightmap.png and voronoi_preview.png
# (128 x 128 px at 256 km/px: 32,768 km around the origin) and voronoi_world.png (128 px wide,
# the whole wrapped world) into <build-dir>/voronoi for a visual check. The summary is printed
# only once the images are written.
add_test(NAME export_voronoi_smoke
    COMMAND terrain_export_voronoi ${CMAKE_BINARY_DIR}/voronoi 20261019 256000 128 0 0 0.5 0.25
            128)
set_tests_properties(export_voronoi_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION
    "world 60031 x 34996 km, 128 x 128 px, 32768\\.0 km.*plates 0\\.50, macro continent 0\\.25")
add_dependencies(check terrain_export_voronoi)

# Invalid arguments of terrain_export_voronoi (plates 0, world size 1, ...) are rejected with
# an error message, before anything is written.
add_test(NAME export_voronoi_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_voronoi>
            -DOUT=${CMAKE_BINARY_DIR}/voronoi_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_voronoi_bad_args.cmake)

# Smoke test: writes cave_slice.png (1024 px wide vertical section through an entrance, with its
# lakes, wall materials and speleothems) and cave_preview.png (224 x 224 px, 20 m/px, around the
# cliff site of the default world, with its lakes) into <build-dir>/caves for a visual check. The
# summary is printed only once the images are written.
string(CONCAT caves_summary
    "224 x 224 px, 4\\.5 km wide, [0-9]+ entrances, caves under [0-9.]+% of the map, "
    "lakes under [0-9.]+%; slice 1024 x [0-9]+ px at [0-9.]+ m/px with [0-9]+ speleothems")
add_test(NAME export_caves_smoke
    COMMAND terrain_export_caves ${CMAKE_BINARY_DIR}/caves)
set_tests_properties(export_caves_smoke PROPERTIES PASS_REGULAR_EXPRESSION "${caves_summary}")
add_dependencies(check terrain_export_caves)

# Smoke test: writes cave_regions.png (512 x 512 px, 100 m/px: 51.2 km of the streamed world
# around the origin, the blended district types with their borders and sites) and
# cave_regions_preview.png (224 x 224 px, 20 m/px, a region with districts around the nearest
# meeting of two types, one of them a Rivers or Volcanic district if possible, with its caves,
# lakes, galleries, rivers, lava and entrances) into <build-dir>/cave_regions for a visual check.
# The summary is printed only once the images are written.
string(CONCAT cave_regions_summary
    "cave regions 512 x 512 px, 51\\.2 km wide, [0-9]+ types over the ground where caves can "
    "exist: tunnels [0-9.]+%.*; preview 224 x 224 px, 4\\.5 km wide at \\(-?[0-9]+, -?[0-9]+\\), "
    "types [a-z]+ [0-9]+%.*, caves under [0-9.]+% of the map, lakes under [0-9.]+%, "
    "galleries over [0-9.]+%, rivers over [0-9.]+%, lava under [0-9.]+%, [0-9]+ entrances")
add_test(NAME export_cave_regions_smoke
    COMMAND terrain_export_cave_regions ${CMAKE_BINARY_DIR}/cave_regions)
set_tests_properties(export_cave_regions_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "${cave_regions_summary}")
add_dependencies(check terrain_export_cave_regions)

# Invalid arguments of terrain_export_cave_regions (size 1, a negative meters-per-pixel, ...)
# are rejected with an error message, before anything is written.
add_test(NAME export_cave_regions_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_cave_regions>
            -DOUT=${CMAKE_BINARY_DIR}/cave_regions_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_cave_regions_bad_args.cmake)

# Smoke test of the far horizon export: the default levels with small windows (64 cells) around
# mountains, written into <build-dir>/horizon. The summary is printed once the images are written.
add_test(NAME export_horizon_smoke
    COMMAND terrain_export_horizon ${CMAKE_BINARY_DIR}/horizon 20261019 -1441792 -3932160 64)
add_dependencies(check terrain_export_horizon)
set_tests_properties(export_horizon_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "viewpoint \\(-1441792, -3932160\\), ground [0-9.]+ m")

# Invalid arguments of the far horizon export (a position without z, non-numeric values, a
# window that is not a multiple of 4 cells, ...) are rejected before anything is written.
add_test(NAME export_horizon_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_horizon>
            -DOUT=${CMAKE_BINARY_DIR}/horizon_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_horizon_bad_args.cmake)
