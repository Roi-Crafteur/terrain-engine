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
terrain_add_test(test_camera_math terrain_viewer_camera)
terrain_add_test(marching_cubes_test terrain_core)
terrain_add_test(noise_test terrain_core)
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
terrain_add_test(persistence_test terrain_core)

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
# (128 x 128 px at 256 km/px: 32,768 km around the origin) and voronoi_world.png (128 px, the
# whole planet) into <build-dir>/voronoi for a visual check. The summary is printed only once
# the images are written.
add_test(NAME export_voronoi_smoke
    COMMAND terrain_export_voronoi ${CMAKE_BINARY_DIR}/voronoi 20261019 256000 128 0 0 0.5 0.25
            128)
set_tests_properties(export_voronoi_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION
    "planet [0-9]+ km, 128 x 128 px, 32768\\.0 km wide.*plates 0\\.50, macro continent 0\\.25")
add_dependencies(check terrain_export_voronoi)

# Invalid arguments of terrain_export_voronoi (plates 0, world size 1, ...) are rejected with
# an error message, before anything is written.
add_test(NAME export_voronoi_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_voronoi>
            -DOUT=${CMAKE_BINARY_DIR}/voronoi_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_voronoi_bad_args.cmake)
