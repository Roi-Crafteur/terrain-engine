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
