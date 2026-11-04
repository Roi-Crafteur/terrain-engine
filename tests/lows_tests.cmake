# The land below the sea level (agent 52, terrain_core/lows.cmake): the tool of its measures.
# Included once from tests/CMakeLists.txt.

# The measures of the land below the sea level of a small world (32 x 16 cells), its map.
add_test(NAME export_lows_smoke
    COMMAND terrain_export_lows ${CMAKE_BINARY_DIR}/lows --world 4194304 2097152 --cell 65536
            --width 32 --readers 4 --threads 2)
set_tests_properties(export_lows_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "lows of seed 20261019, world 4194 x 2097 km.*closed lows: [0-9]+")
add_dependencies(check terrain_export_lows)

# Invalid arguments of terrain_export_lows are rejected before anything is computed.
add_test(NAME export_lows_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_lows>
            -DOUT=${CMAKE_BINARY_DIR}/lows_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_lows_bad_args.cmake)
