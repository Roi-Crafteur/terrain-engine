# Pure generation library: must never depend on OpenGL, GLFW, glad or a window.
add_library(terrain_core STATIC
    src/chunk_coord.cpp
    src/generation/eroded_region.cpp
    src/generation/erosion.cpp
    src/generation/height_grid.cpp
    src/generation/heightfield.cpp
    src/generation/heightfield_density.cpp
    src/generation/noise.cpp
    src/generation/rivers.cpp
    src/io/eroded_png.cpp
    src/io/heightmap_png.cpp
    src/io/png.cpp
    src/meshing/marching_cubes.cpp
)

target_include_directories(terrain_core PUBLIC include)
target_compile_features(terrain_core PUBLIC cxx_std_20)
target_link_libraries(terrain_core PRIVATE terrain_warnings)

# Heightmap PNG export for visual checks: terrain_export_heightmap <output-dir> [seed] [m/px]
add_executable(terrain_export_heightmap tools/export_heightmap.cpp)
target_link_libraries(terrain_export_heightmap PRIVATE terrain_core terrain_warnings)

# Eroded region PNG export: terrain_export_eroded <output-dir> [seed] [m/px] [size]
add_executable(terrain_export_eroded tools/export_eroded.cpp)
target_link_libraries(terrain_export_eroded PRIVATE terrain_core terrain_warnings)
