# The water level of the outputs for Unreal (task 7b). The tests of a task live in a file of
# their own, included once from tests/CMakeLists.txt, so that the list stays under 500 lines.
terrain_add_test(water_level_test terrain_core)
# ATerrainWorld::PlanetFile reaches the session (the plugin itself is only built by Unreal).
add_test(NAME unreal_planet_file
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/unreal_planet_file.cmake)
