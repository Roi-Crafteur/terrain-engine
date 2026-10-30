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
