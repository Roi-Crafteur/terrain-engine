# The smoke tests and the invalid arguments of the tools of the relief, the views, the blends,
# the water and the planet level (terrain_export_relief, _view, _blend, _water, _planet).
# Included once from tests/CMakeLists.txt, at the place where they were defined.

# Maps and figures of the relief of a small world (64 x 32 cells of 64 km, zooms of 64 px), with
# the zooms of a place given by --at.
add_test(NAME export_relief_smoke
    COMMAND terrain_export_relief ${CMAKE_BINARY_DIR}/relief 20261019 default 65536 64 2 4194304
            2097152 --at place 1000 -2000)
set_tests_properties(export_relief_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "relief default, seed 20261019, world 4194 x 2097 km, cells of 65536 m")
add_dependencies(check terrain_export_relief)

# Invalid arguments of terrain_export_relief are rejected before anything is computed.
add_test(NAME export_relief_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_relief>
            -DOUT=${CMAKE_BINARY_DIR}/relief_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_relief_bad_args.cmake)

# Views in perspective of the relief of a small world (64 x 36 pixels).
add_test(NAME export_view_smoke
    COMMAND terrain_export_view ${CMAKE_BINARY_DIR}/views 20261019 default 64 65536 2 4194304
            2097152)
set_tests_properties(export_view_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "relief default, seed 20261019, world 4194 x 2097 km, views of 64 x 36")
add_dependencies(check terrain_export_view)

# Invalid arguments of terrain_export_view are rejected before anything is computed.
add_test(NAME export_view_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_view>
            -DOUT=${CMAKE_BINARY_DIR}/view_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_view_bad_args.cmake)

# Maps of the transitions of 64 x 64 pixels around the origin.
add_test(NAME export_blend_smoke
    COMMAND terrain_export_blend maps ${CMAKE_BINARY_DIR}/blend_maps --size 64 --threads 2
            --at origin 0 0)
set_tests_properties(export_blend_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "origin at \\(0, 0\\): maps of 200, 20 and 2 km written")
add_dependencies(check terrain_export_blend)

# Invalid arguments of terrain_export_blend are rejected before anything is computed.
add_test(NAME export_blend_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_blend>
            -DOUT=${CMAKE_BINARY_DIR}/blend_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_blend_bad_args.cmake)

# Maps and views of the water of a small world (maps of 64 px, a view of 64 x 36 pixels).
add_test(NAME export_water_smoke
    COMMAND terrain_export_water ${CMAKE_BINARY_DIR}/water --world 4194304 2097152 --threads 2
            --cell 65536 --width 64 --pixels 64 --map here 1000 -2000 --view here 1000 -2000 90)
set_tests_properties(export_water_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "water of seed 20261019, world 4194 x 2097 km")
add_dependencies(check terrain_export_water)

# Invalid arguments of terrain_export_water are rejected before anything is computed.
add_test(NAME export_water_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_water>
            -DOUT=${CMAKE_BINARY_DIR}/water_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_water_bad_args.cmake)

# The planet level of a small world (128 x 64 cells of 16 km), its figures and images.
add_test(NAME export_planet_smoke
    COMMAND terrain_export_planet ${CMAKE_BINARY_DIR}/planet 20261019 16384 default 1 2 2097152
            1048576)
set_tests_properties(export_planet_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "default relief, 128 x 64 cells of 16384 m, 1 x 1 samples, 2 threads")
add_dependencies(check terrain_export_planet)

# Invalid arguments of terrain_export_planet (cell size not dividing the world, unknown relief,
# 0 samples...) are rejected with an error message before anything is computed.
add_test(NAME export_planet_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_planet>
            -DOUT=${CMAKE_BINARY_DIR}/planet_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_planet_bad_args.cmake)
