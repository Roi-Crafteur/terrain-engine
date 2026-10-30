# The water drawn in Unreal at its level (agent 27): the tests of the probe of the water, of the
# water data of the tiles (core) and of the water meshes and textures of the Bridge. Included
# once, at the end of tests/CMakeLists.txt (after the Bridge library), so that its list stays
# under 500 lines.

# Invalid arguments of the probe of the water are rejected before any planet file is read.
add_test(NAME probe_water_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_probe_water>
            -DOUT=${CMAKE_BINARY_DIR}/probe_water_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/probe_water_bad_args.cmake)
add_dependencies(check terrain_probe_water)
