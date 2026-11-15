# The smoke tests and the invalid arguments of the export tools of the first phases (heightmap,
# eroded, cliffs, arches, biomes, voronoi, caves, cave regions, far horizon, silhouette).
# Included once from tests/CMakeLists.txt, at the place where they were defined.

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

# Smoke test of the silhouette measure: small windows (64 cells), one column per degree, the
# default crests, written into <build-dir>/silhouette. Its first line comes once the truth of
# the first place is traced.
add_test(NAME export_silhouette_smoke
    COMMAND terrain_export_silhouette ${CMAKE_BINARY_DIR}/silhouette 64 1)
add_dependencies(check terrain_export_silhouette)
set_tests_properties(export_silhouette_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "k2_valley: \\(416857, -11398966\\), ground [0-9.]+ m")

# Invalid arguments of the silhouette measure (windows, columns per degree, crest ratios) are
# rejected before anything is written.
add_test(NAME export_silhouette_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_silhouette>
            -DOUT=${CMAKE_BINARY_DIR}/silhouette_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_silhouette_bad_args.cmake)
