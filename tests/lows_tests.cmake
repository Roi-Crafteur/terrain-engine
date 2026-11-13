# The land below the sea level (agent 52, terrain_core/lows.cmake): the readers that told the sea
# from the height, the tool of its measures. Included once from tests/CMakeLists.txt.
terrain_add_test(lows_readers_test terrain_core)
terrain_add_test(lows_sites_test terrain_core)
terrain_add_test(lows_flags_test terrain_core)
# The impact craters: their forms, their wear, their sites on the default world.
terrain_add_test(lows_craters_test terrain_core)
target_include_directories(lows_craters_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
# The hollows of the plains: kettles and pans by the pre-climate, never raising the ground.
terrain_add_test(lows_hollows_test terrain_core)
# The points of the audit: the sunken sedimentary basins of the geography, the depressions below
# the sea level of the planet level sealed by their way out.
terrain_add_test(lows_sunken_test terrain_core)
terrain_add_test(lows_breach_test terrain_core)
# Every low on: the sea never moves; the cost of the samples on the plains and in the mountains.
terrain_add_test(lows_sea_cost_test terrain_core)

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
