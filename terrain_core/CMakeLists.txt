# Floating-point rules of the generation. Every client generates the world from the seed, so
# every result must be the same bit for bit with MSVC, GCC, Clang and AppleClang, on x86-64 and
# ARM64: no contraction of a * b + c into a fused multiply-add (ARM64 always has one, and GCC and
# Clang fuse by default) and no fast math. Public, so that the tests, the tools and the viewer
# compile the inline functions of the headers with the same rules.
add_library(terrain_fp_strict INTERFACE)
foreach(flags_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
    set(fast "-ffast-math|-Ofast|-funsafe-math-optimizations")
    if("${${flags_var}}" MATCHES "${fast}|-ffp-contract=(fast|on)|[/-]fp:(fast|contract)")
        message(FATAL_ERROR "${flags_var} holds a floating-point option that breaks the "
                            "determinism of terrain_core: ${${flags_var}}")
    endif()
endforeach()
if(MSVC)
    # Visual Studio 2022 is the first version whose /fp:precise never contracts.
    if(MSVC_VERSION LESS 1930)
        message(FATAL_ERROR "terrain_core needs MSVC 19.30 (Visual Studio 2022) or later: "
                            "before it, /fp:precise allows floating-point contractions")
    endif()
    target_compile_options(terrain_fp_strict INTERFACE /fp:precise)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # clang-cl maps /fp:precise to contractions within a statement.
        target_compile_options(terrain_fp_strict INTERFACE /clang:-ffp-contract=off)
    endif()
else()
    target_compile_options(terrain_fp_strict INTERFACE -ffp-contract=off -fno-fast-math)
endif()

# Pure generation library: must never depend on OpenGL, GLFW, glad or a window.
add_library(terrain_core STATIC
    src/chunk_coord.cpp
    src/det_math.cpp
    src/generation/aquifers.cpp
    src/generation/arches.cpp
    src/generation/biome.cpp
    src/generation/biome_blend.cpp
    src/generation/biome_margins.cpp
    src/generation/cave_entrances.cpp
    src/generation/cave_flora.cpp
    src/generation/cave_galleries.cpp
    src/generation/cave_materials.cpp
    src/generation/cave_noise.cpp
    src/generation/cave_regions.cpp
    src/generation/cave_region.cpp
    src/generation/cave_sampler.cpp
    src/generation/caves.cpp
    src/generation/cliff_region.cpp
    src/generation/cliffs.cpp
    src/generation/climate.cpp
    src/generation/depression_forms.cpp
    src/generation/depressions.cpp
    src/generation/ecology.cpp
    src/generation/eroded_region.cpp
    src/generation/geography.cpp
    src/generation/geography_crust.cpp
    src/generation/geography_flood.cpp
    src/generation/geography_grid.cpp
    src/generation/geography_provinces.cpp
    src/generation/geography_relief.cpp
    src/generation/geography_boundaries.cpp
    src/generation/geography_tectonics.cpp
    src/generation/erosion.cpp
    src/generation/height_grid.cpp
    src/generation/heightfield.cpp
    src/generation/heightfield_density.cpp
    src/generation/heightfield_geography.cpp
    src/generation/heightfield_hydrology.cpp
    src/generation/heightfield_water.cpp
    src/generation/noise.cpp
    src/generation/noise_octaves.cpp
    src/generation/overhangs.cpp
    src/generation/planet/planet_breach.cpp
    src/generation/planet/planet_build.cpp
    src/generation/planet/planet_climate.cpp
    src/generation/planet/planet_compact.cpp
    src/generation/planet/planet_false_passes.cpp
    src/generation/planet/planet_features.cpp
    src/generation/planet/planet_flow.cpp
    src/generation/planet/planet_grid.cpp
    src/generation/planet/planet_hierarchy.cpp
    src/generation/planet/planet_lakes.cpp
    src/generation/planet/planet_level.cpp
    src/generation/planet/planet_moisture.cpp
    src/generation/planet/planet_order.cpp
    src/generation/planet/planet_parallel.cpp
    src/generation/planet/planet_relief.cpp
    src/generation/planet/planet_rivers.cpp
    src/generation/planet/planet_stats.cpp
    src/generation/planet/planet_wind.cpp
    src/generation/mountain_forms.cpp
    src/generation/mountain_walls.cpp
    src/generation/plain_forms.cpp
    src/generation/ravines.cpp
    src/generation/relief_local.cpp
    src/generation/rivers.cpp
    src/generation/sculpted_region.cpp
    src/generation/speleothems.cpp
    src/generation/surface_material.cpp
    src/generation/terrain_surface.cpp
    src/generation/vegetation.cpp
    src/generation/voronoi.cpp
    src/generation/water/fine_lake.cpp
    src/generation/water/hydrology.cpp
    src/generation/water/hydrology_lakes.cpp
    src/generation/water/hydrology_query.cpp
    src/generation/water/hydrology_region.cpp
    src/generation/water/river_box.cpp
    src/generation/water/river_course.cpp
    src/generation/water/river_falls.cpp
    src/generation/water/river_join.cpp
    src/generation/water/river_path.cpp
    src/generation/water/river_profile.cpp
    src/generation/water/river_slot.cpp
    src/generation/voronoi_layout.cpp
    src/io/arches_png.cpp
    src/io/blend_png.cpp
    src/io/cave_regions_png.cpp
    src/io/caves_png.cpp
    src/io/caves_png_layers.cpp
    src/io/caves_png_network.cpp
    src/io/cliffs_png.cpp
    src/io/ecology_png.cpp
    src/io/eroded_png.cpp
    src/io/heightmap_png.cpp
    src/io/horizon_panorama.cpp
    src/io/horizon_png.cpp
    src/io/horizon_silhouette.cpp
    src/io/relief_lakes.cpp
    src/io/relief_rifts.cpp
    src/io/planet_png.cpp
    src/io/planet_png_layers.cpp
    src/io/png.cpp
    src/io/relief_png.cpp
    src/io/relief_stats.cpp
    src/io/relief_stats_local.cpp
    src/io/relief_stats_shapes.cpp
    src/io/relief_stats_peaks.cpp
    src/io/relief_stats_terrain.cpp
    src/io/relief_view.cpp
    src/io/voronoi_png.cpp
    src/meshing/marching_cubes.cpp
    src/storage/block_section.cpp
    src/storage/chunk_record.cpp
    src/storage/durable_file.cpp
    src/storage/edit_lattice.cpp
    src/storage/edit_summary.cpp
    src/storage/edit_window.cpp
    src/storage/file_commit.cpp
    src/storage/frame_reader.cpp
    src/storage/modifications.cpp
    src/storage/planet_file.cpp
    src/storage/region_file.cpp
    src/storage/section_codec.cpp
    src/storage/storage_codec.cpp
    src/storage/volume_edit.cpp
    src/storage/world_directory.cpp
    src/storage/world_meta.cpp
    src/storage/world_save.cpp
    src/streaming/cave_tiles.cpp
    src/streaming/horizon_crests.cpp
    src/streaming/horizon_epoch.cpp
    src/streaming/horizon_grids.cpp
    src/streaming/horizon_levels.cpp
    src/streaming/horizon_normals.cpp
    src/streaming/horizon_sampler.cpp
    src/streaming/horizon_streamer.cpp
    src/streaming/lod.cpp
    src/streaming/stream_manager.cpp
    src/streaming/terrain_tiles.cpp
    src/streaming/tile_blend.cpp
    src/world_size.cpp
)

target_include_directories(terrain_core PUBLIC include)
target_compile_features(terrain_core PUBLIC cxx_std_20)
# The streaming generates tiles on worker threads (std::jthread): threads only, nothing graphic.
find_package(Threads REQUIRED)
target_link_libraries(terrain_core
    PUBLIC Threads::Threads terrain_fp_strict
    PRIVATE terrain_warnings)

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

# Tectonic plate (Voronoi) PNG export: voronoi_plates.png, voronoi_heightmap.png,
# voronoi_preview.png of a region and voronoi_world.png of the whole planet:
# terrain_export_voronoi <output-dir> [seed] [m/px] [size] [center-x] [center-z] [plates]
#                        [macro-continent] [world-size]
add_executable(terrain_export_voronoi tools/export_voronoi.cpp)
target_link_libraries(terrain_export_voronoi PRIVATE terrain_core terrain_warnings)

# Planet maps before and after the plate layer. Not part of the tests (about a minute in an
# unoptimized build):
#   cmake --build <build-dir> --target export_voronoi_maps
# writes into <build-dir>/voronoi_maps, 4096 px at 8 km/px (32,768 km) around the origin:
# - before_heightmap_32768km.png and before_preview_32768km.png: the macro layer alone (0.5),
#   the planetary option before the plates;
# - voronoi_plates.png, voronoi_heightmap.png and voronoi_preview.png: plates 0.5 with
#   macro 0.25. The planet is 45,000 to 65,000 km wide, so this is a local zoom;
# - voronoi_world.png: the whole planet (one period of the plate layout) in 2048 px.
set(voronoi_maps ${CMAKE_BINARY_DIR}/voronoi_maps)
add_custom_target(export_voronoi_maps
    COMMAND ${CMAKE_COMMAND} -E make_directory ${voronoi_maps}
    COMMAND terrain_export_heightmap ${voronoi_maps}/before 20261019 8000 4096 0 0 0.5
    COMMAND terrain_export_voronoi ${voronoi_maps} 20261019 8000 4096 0 0 0.5 0.25 2048
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${voronoi_maps}/before/heightmap.png
            ${voronoi_maps}/before_heightmap_32768km.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${voronoi_maps}/before/heightmap_preview.png
            ${voronoi_maps}/before_preview_32768km.png
    VERBATIM
    USES_TERMINAL
)

# Benchmark of the noise and the relief, not a test (outside ctest): heightAt, sampleAt, the
# tiles of LOD 0 to 3 with and without caves, 1024 x 1024 height grids and the parts of a height
# sample, with a checksum of every result that an optimization must leave unchanged. Build in
# Release: terrain_bench_noise [runs] [--quick]
add_executable(terrain_bench_noise tools/bench_noise.cpp tools/bench_world.cpp)
target_link_libraries(terrain_bench_noise PRIVATE terrain_core terrain_warnings)
# The surface blend of the tiles, internal to terrain_core (src/streaming/tile_blend.hpp).
target_include_directories(terrain_bench_noise PRIVATE src/streaming)

# Sizes, times and memory of the modifications by sections against the criteria of phase 15
# (a hole of 10 m, a mountain of 1 km x 1 km x 500 m razed), not a test (outside ctest). Build
# in Release: terrain_modification_sizes <output-dir> [runs]; then compare with the full chunks
# of Minecraft: python3 scripts/minecraft_chunk_sizes.py <output-dir>/*.chunks
add_executable(terrain_modification_sizes tools/modification_sizes.cpp)
target_link_libraries(terrain_modification_sizes PRIVATE terrain_core terrain_warnings)

# Planet level (climate, lakes, river graph) of a whole world, its figures and control images:
# terrain_export_planet <output-dir> [seed] [cell-size] [default|planetary] [samples-per-axis]
#                       [threads] [world-x] [world-z]
add_executable(terrain_export_planet tools/export_planet.cpp)
target_link_libraries(terrain_export_planet PRIVATE terrain_core terrain_warnings)

# The planet level of the default world (seed 20261019, cells of 8,192 m) with the planetary
# relief and with the default one, one sample per cell. Not part of the tests (about a minute
# each with 8 threads in Release, a few hundred MB kept and about 1.4 GB at the peak):
#   cmake --build <build-dir> --target export_planet_maps
# writes the figures to the terminal and the images into <build-dir>/planet_maps/planetary and
# <build-dir>/planet_maps/default.
set(planet_maps ${CMAKE_BINARY_DIR}/planet_maps)
add_custom_target(export_planet_maps
    COMMAND ${CMAKE_COMMAND} -E make_directory ${planet_maps}
    COMMAND terrain_export_planet ${planet_maps}/planetary 20261019 8192 planetary 1
    COMMAND terrain_export_planet ${planet_maps}/default 20261019 8192 default 1
    VERBATIM
    USES_TERMINAL
)

# Maps and figures of a relief, to compare reliefs (phase 11R): the planet, zooms of 2,048,
# 200 and 20 km on a chain, a plain and a coast, land masses, chains, hypsometry, coasts:
# terrain_export_relief <output-dir> [seed] [default|planetary|legacy|geography] [cell-size]
#                       [zoom-pixels] [threads] [world-x] [world-z] [--at <name> <x> <z>]...
add_executable(terrain_export_relief tools/export_relief.cpp)
target_link_libraries(terrain_export_relief PRIVATE terrain_core terrain_warnings)

# Views of a relief in perspective, 70 degrees wide, at the foot of the longest chain, in a
# plain, from the highest summit, from 300 m above a chain, from the floor of a great valley and at
# the foot of a wall, with a tower of 330 m 2 km away:
# terrain_export_view <output-dir> [seed] [default|planetary|legacy|geography] [width]
#                     [cell-size] [threads] [world-x] [world-z]
add_executable(terrain_export_view tools/export_view.cpp tools/view_places.cpp)
target_link_libraries(terrain_export_view PRIVATE terrain_core terrain_warnings)

# Maps and views of the water of a world (phase 11B): the sea, the depressions below the sea
# level. See the comment at the top of tools/export_water.cpp.
# terrain_export_water <output-dir> [--seed <seed>] [--world <x> <z>] [--threads <n>]
#                      [--cell <meters>] [--width <pixels>] [--map <name> <x> <z>]...
#                      [--view <name> <x> <z> <heading-degrees>]...
add_executable(terrain_export_water tools/export_water.cpp)
target_link_libraries(terrain_export_water PRIVATE terrain_core terrain_warnings)

# Maps and views of the rivers and lakes of a world (phase 11B), its planet level read from (or
# built and written to) --planet:
# terrain_export_rivers <output-dir> [--planet <file>] [--seed <seed>] [--world <x> <z>]
#                       [--threads <n>] [--width <pixels>] [--pixels <map-pixels>]
#                       [--map <name> <x> <z>]... [--view <name> <x> <z> <heading> <above>]...
add_executable(terrain_export_rivers tools/export_rivers.cpp tools/rivers_places.cpp
                                     tools/rivers_report.cpp)
target_link_libraries(terrain_export_rivers PRIVATE terrain_core terrain_warnings)

# The planet level of a world written to its file, as the game reads it (phase 11B):
# terrain_make_planet <file> [--seed <seed>] [--world <x> <z>] [--threads <n>]
add_executable(terrain_make_planet tools/make_planet.cpp)
target_link_libraries(terrain_make_planet PRIVATE terrain_core terrain_warnings)

# The maps of the default world (seed 20261019) with the default and the planetary relief, into
# <build-dir>/relief_maps/default and <build-dir>/relief_maps/planetary. Not part of the tests
# (about a minute each in Release):
#   cmake --build <build-dir> --target export_relief_maps
set(relief_maps ${CMAKE_BINARY_DIR}/relief_maps)
add_custom_target(export_relief_maps
    COMMAND ${CMAKE_COMMAND} -E make_directory ${relief_maps}
    COMMAND terrain_export_relief ${relief_maps}/default 20261019 default
    COMMAND terrain_export_relief ${relief_maps}/planetary 20261019 planetary
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

# Biome, surface material and vegetation density PNG export, on an eroded region:
# terrain_export_biomes <output-dir> [seed] [m/px] [size] [center-x] [center-z]
add_executable(terrain_export_biomes tools/export_biomes.cpp)
target_link_libraries(terrain_export_biomes PRIVATE terrain_core terrain_warnings)

# Biome and material maps before and after the transitions (the colors mixed by the weights of
# the surface blend) at 200, 20 and 2 km around given places, and the texels of LOD 0 tiles on an
# edge between sand and rock against the blend every 0.5 m:
# terrain_export_blend maps <output-dir> [--seed <s>] [--size <px>] [--threads <n>]
#                      --at <name> <x> <z>...
# terrain_export_blend edge <output-dir> <x> <z> [--seed <s>]
add_executable(terrain_export_blend tools/export_blend.cpp)
target_link_libraries(terrain_export_blend PRIVATE terrain_core terrain_warnings)

# Cave PNG export, on an eroded region with its rivers around the cliff site (224 px at 20 m/px
# by default): cave_slice.png (vertical section through an entrance) and cave_preview.png (map
# of the caves, entrances and rivers): terrain_export_caves <output-dir> [seed] [m/px] [size]
add_executable(terrain_export_caves tools/export_caves.cpp)
target_link_libraries(terrain_export_caves PRIVATE terrain_core terrain_warnings)

# Cave regions PNG export: cave_regions.png (map of the district types of the streamed world,
# 512 px at 100 m/px) and cave_regions_preview.png (a CaveRegion with its districts around a
# meeting of two types): terrain_export_cave_regions <output-dir> [seed] [m/px] [size] [cx] [cz]
add_executable(terrain_export_cave_regions tools/export_cave_regions.cpp)
target_link_libraries(terrain_export_cave_regions PRIVATE terrain_core terrain_warnings)

# Far horizon PNG export: level_N.png, map_relief.png, map_materials.png and panorama.png (360
# degrees from a summit found with the grids, or from x z), with how far the relief shows and
# what the silhouette loses against finer point samples:
# terrain_export_horizon <output-dir> [seed] [x z] [cells-per-side]
add_executable(terrain_export_horizon tools/export_horizon.cpp)
target_link_libraries(terrain_export_horizon PRIVATE terrain_core terrain_warnings)

# Silhouette of the far horizon against the truth (heightAt along each ray) at the valley of the
# K2 and at the foot of the longest chain, before and with the crests, with images and timings:
# terrain_export_silhouette <output-dir> [--timings] [cells-per-side] [columns-per-degree]
#     [ratio...]
add_executable(terrain_export_silhouette tools/export_silhouette.cpp)
target_link_libraries(terrain_export_silhouette PRIVATE terrain_core terrain_warnings)
