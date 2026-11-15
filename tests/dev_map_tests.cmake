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

# The Bridge of the dev map (pure C++ of the Unreal plugin): its views, its tiles on threads of
# their own, where it sends the player.
target_sources(terrain_unreal_bridge PRIVATE
    ${unreal_bridge}/map_landing.cpp
    ${unreal_bridge}/map_places_file.cpp
    ${unreal_bridge}/map_targets.cpp
    ${unreal_bridge}/map_tile_cache.cpp
    ${unreal_bridge}/map_view.cpp)
terrain_add_test(unreal_map_view_test terrain_unreal_bridge)
terrain_add_test(unreal_map_cache_test terrain_unreal_bridge)
terrain_add_test(unreal_map_targets_test terrain_unreal_bridge)
terrain_add_test(unreal_map_landing_test terrain_unreal_bridge)
# The places of the map on the file of agent 36: a copy of data/dev/places.json read and written,
# its original places kept (tests/frozen/places_reference.json).
add_executable(unreal_map_places_file_test unreal_map_places_file_test.cpp)
target_link_libraries(unreal_map_places_file_test PRIVATE terrain_unreal_bridge terrain_warnings)
add_test(NAME unreal_map_places_file_test
    COMMAND unreal_map_places_file_test ${PROJECT_SOURCE_DIR}/data/dev/places.json
            ${CMAKE_CURRENT_BINARY_DIR}/unreal_map_places_file
            ${CMAKE_CURRENT_SOURCE_DIR}/frozen/places_reference.json)
add_dependencies(check unreal_map_places_file_test)
add_test(NAME unreal_dev_map_sources
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/unreal_dev_map_sources.cmake)
