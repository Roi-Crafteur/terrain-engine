# The tests of the settlement V1 (agent 25, docs/PEUPLEMENT.md): data files, analysis maps,
# peoples, kingdoms. The tests of a task live in a file of their own, included once from
# tests/CMakeLists.txt, so that the list stays short.

# The JSON of the data files: strict reading, numbers to the bit, canonical writing.
terrain_add_test(json_test terrain_core)

# The scores of section 6: trapezoid with soft floor and hard limits, ramps, physical score.
terrain_add_test(settlement_preference_test terrain_core)

# The data files of the repository read whole; invalid files refused with a clear message; a new
# resource is a new file.
terrain_add_test(settlement_data_test terrain_core)
target_compile_definitions(settlement_data_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement"
    TERRAIN_TEST_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/settlement_data_scratch")

# The geology in data and the public magic network on the default world: the plates made again
# exactly as the geography makes them (a guard against a change of the geography), the counts of
# the configuration, the arcs on the overriding plates, the same bits whatever the threads.
terrain_add_test(settlement_geology_test terrain_core)
target_include_directories(settlement_geology_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
target_compile_definitions(settlement_geology_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement")

# The pieces of the analysis maps on hand-made inputs: distances exact on the torus, the scores of
# a cell, the habitability over its theoretical maximum, the resources in deposits.
terrain_add_test(settlement_maps_test terrain_core)
target_include_directories(settlement_maps_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation/settlement)
target_compile_definitions(settlement_maps_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement")

# The settlement on the default world in cells of 65,536 m: variety of the world, roughness
# against the real relief, maps and resources where their rules put them, a magic network fair to
# every terrain, the same bits whatever the threads.
terrain_add_test(settlement_world_test terrain_core)
target_compile_definitions(settlement_world_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement")

# The tool on a small world: the maps, the report and the images.
add_test(NAME export_settlement_smoke
    COMMAND terrain_export_settlement ${CMAKE_BINARY_DIR}/settlement_smoke --world 2097152 1048576
            --cell 16384 --threads 2 --width 128 --preset all --zoom 0 0 800 400)
set_tests_properties(export_settlement_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION
    "Spearman .*images in .*zoom: 49 x 24 cells.*preset civilized.*peak resident")
add_test(NAME export_settlement_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_settlement>
            -DOUT=${CMAKE_BINARY_DIR}/settlement_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_settlement_bad_args.cmake)
# A planet file given with --planet is never written in silence.
add_test(NAME export_settlement_planet_file
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_settlement>
            -DDIR=${CMAKE_BINARY_DIR}/settlement_planet_file
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_settlement_planet_file.cmake)

# The peoples: the 13 files of the repository, the merge over the base terrain model, the
# relations, invalid files refused with a clear message; raceSuitability (its maximum, the hard
# limits) on the default world in cells of 65,536 m, the same bits whatever the threads.
terrain_add_test(settlement_peoples_test terrain_core)
target_compile_definitions(settlement_peoples_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement"
    TERRAIN_TEST_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/settlement_peoples_scratch")

# The world presets and the states: draws in the ranges of the preset from the seed, forced
# values, the rounding of the shares of the peoples, the target areas; the capitals of the
# default world in cells of 65,536 m, in the most habitable fifth and near fresh water.
terrain_add_test(settlement_presets_test terrain_core)
target_compile_definitions(settlement_presets_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement")

# The kingdoms of the default world in cells of 65,536 m for the three presets: the tests of a good
# world (a state for each people, at most 15 % of the land a kingdom and 40 % of the land held a
# people, at least half of the land borders natural, the capitals, the hostile land nearly empty),
# the same bits whatever the threads.
terrain_add_test(settlement_kingdoms_test terrain_core)
target_compile_definitions(settlement_kingdoms_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement")

# The expansion and the borders on hand-made grids: borders on a river and on a ridge, the coast,
# the target area and its barriers, an unlivable land, the straits, the cleanup of the artifacts.
terrain_add_test(settlement_expansion_test terrain_core)
