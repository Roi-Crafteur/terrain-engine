# Pure generation library: must never depend on OpenGL, GLFW, glad or a window.
add_library(terrain_core STATIC
    src/chunk_coord.cpp
    src/generation/arches.cpp
    src/generation/cliff_region.cpp
    src/generation/cliffs.cpp
    src/generation/eroded_region.cpp
    src/generation/erosion.cpp
    src/generation/height_grid.cpp
    src/generation/heightfield.cpp
    src/generation/heightfield_density.cpp
    src/generation/noise.cpp
    src/generation/overhangs.cpp
    src/generation/rivers.cpp
    src/generation/sculpted_region.cpp
    src/io/arches_png.cpp
    src/io/cliffs_png.cpp
    src/io/eroded_png.cpp
    src/io/heightmap_png.cpp
    src/io/png.cpp
    src/meshing/marching_cubes.cpp
)

target_include_directories(terrain_core PUBLIC include)
target_compile_features(terrain_core PUBLIC cxx_std_20)
target_link_libraries(terrain_core PRIVATE terrain_warnings)

# Heightmap PNG export for visual checks:
# terrain_export_heightmap <output-dir> [seed] [m/px] [size] [center-x] [center-z]
#                          [macro-continent]
add_executable(terrain_export_heightmap tools/export_heightmap.cpp)
target_link_libraries(terrain_export_heightmap PRIVATE terrain_core terrain_warnings)

# Large heightmaps to judge the layout of continents, oceans and mountains: 4096 x 4096 px
# around the origin, 409.6 km wide at 100 m/px and 1024 km wide at 250 m/px. Not part of the
# tests (about half a minute per map in an unoptimized build):
#   cmake --build <build-dir> --target export_large_heightmaps
# writes heightmap_409km.png, heightmap_preview_409km.png, heightmap_1024km.png and
# heightmap_preview_1024km.png into <build-dir>/heightmap_large.
set(large_heightmaps ${CMAKE_BINARY_DIR}/heightmap_large)
add_custom_target(export_large_heightmaps
    COMMAND ${CMAKE_COMMAND} -E make_directory ${large_heightmaps}
    COMMAND terrain_export_heightmap ${large_heightmaps}/409km 20261019 100 4096 0 0
    COMMAND terrain_export_heightmap ${large_heightmaps}/1024km 20261019 250 4096 0 0
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${large_heightmaps}/409km/heightmap.png
            ${large_heightmaps}/heightmap_409km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${large_heightmaps}/409km/heightmap_preview.png
            ${large_heightmaps}/heightmap_preview_409km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${large_heightmaps}/1024km/heightmap.png
            ${large_heightmaps}/heightmap_1024km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${large_heightmaps}/1024km/heightmap_preview.png
            ${large_heightmaps}/heightmap_preview_1024km.png
    VERBATIM
    USES_TERMINAL
)

# Maps without and with the macro-continental layer (macro continent 0 and 0.5), to compare
# them. Not part of the tests (about half a minute per map in an unoptimized build):
#   cmake --build <build-dir> --target export_macro_heightmaps
# writes into <build-dir>/heightmap_macro:
# - heightmap_1024km.png, heightmap_preview_1024km.png (without the layer),
#   heightmap_macro_1024km.png and heightmap_macro_preview_1024km.png (with it): 4096 px at
#   250 m/px around (-5312, 4672) km, the most contrasted macro coast of the default world
#   within 8000 km of the origin, with an ocean to the north (top) and a continent to the
#   south. The origin always lies on a macro coast, where the layer hardly changes the map.
# - heightmap[_preview]_32768km.png and heightmap_macro[_preview]_32768km.png: the planet
#   scale, 4096 px at 8 km/px around the origin, without and with the layer.
set(macro_heightmaps ${CMAKE_BINARY_DIR}/heightmap_macro)
set(macro_coast -5312000 4672000)
add_custom_target(export_macro_heightmaps
    COMMAND ${CMAKE_COMMAND} -E make_directory ${macro_heightmaps}
    COMMAND terrain_export_heightmap ${macro_heightmaps}/1024km 20261019 250 4096 ${macro_coast} 0
    COMMAND terrain_export_heightmap ${macro_heightmaps}/macro_1024km 20261019 250 4096
            ${macro_coast} 0.5
    COMMAND terrain_export_heightmap ${macro_heightmaps}/32768km 20261019 8000 4096 0 0 0
    COMMAND terrain_export_heightmap ${macro_heightmaps}/macro_32768km 20261019 8000 4096 0 0 0.5
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${macro_heightmaps}/1024km/heightmap.png
            ${macro_heightmaps}/heightmap_1024km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${macro_heightmaps}/1024km/heightmap_preview.png
            ${macro_heightmaps}/heightmap_preview_1024km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${macro_heightmaps}/macro_1024km/heightmap.png
            ${macro_heightmaps}/heightmap_macro_1024km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${macro_heightmaps}/macro_1024km/heightmap_preview.png
            ${macro_heightmaps}/heightmap_macro_preview_1024km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${macro_heightmaps}/32768km/heightmap.png
            ${macro_heightmaps}/heightmap_32768km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${macro_heightmaps}/32768km/heightmap_preview.png
            ${macro_heightmaps}/heightmap_preview_32768km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${macro_heightmaps}/macro_32768km/heightmap.png
            ${macro_heightmaps}/heightmap_macro_32768km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${macro_heightmaps}/macro_32768km/heightmap_preview.png
            ${macro_heightmaps}/heightmap_macro_preview_32768km.png
    VERBATIM
    USES_TERMINAL
)

# Eroded region PNG export: terrain_export_eroded <output-dir> [seed] [m/px] [size]
add_executable(terrain_export_eroded tools/export_eroded.cpp)
target_link_libraries(terrain_export_eroded PRIVATE terrain_core terrain_warnings)

# Cliff PNG export: terrain_export_cliffs <output-dir> [seed] [m/px] [size]
add_executable(terrain_export_cliffs tools/export_cliffs.cpp)
target_link_libraries(terrain_export_cliffs PRIVATE terrain_core terrain_warnings)

# Overhang and arch PNG export: terrain_export_arches <output-dir> [seed] [m/px] [size]
add_executable(terrain_export_arches tools/export_arches.cpp)
target_link_libraries(terrain_export_arches PRIVATE terrain_core terrain_warnings)
