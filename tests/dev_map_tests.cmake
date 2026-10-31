# The tests of the dev map (agent 35): images of the world seen from above, their layers, their
# tiles, and what Unreal makes of them (the Bridge). A task's tests live in their own file.
terrain_add_test(map_frame_test terrain_core)
terrain_add_test(map_layers_test terrain_core)
terrain_add_test(map_render_test terrain_core)
terrain_add_test(map_cost_test terrain_core)
terrain_add_test(map_probe_test terrain_core)
terrain_add_test(map_concurrency_test terrain_core)
add_test(NAME export_map_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_export_map>
            -DOUT=${CMAKE_BINARY_DIR}/export_map_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/export_map_bad_args.cmake)
add_dependencies(check terrain_export_map)
