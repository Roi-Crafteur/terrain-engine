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
