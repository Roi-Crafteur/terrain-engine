# The experimental flags of the world (agent 51): the relief of the wave merged switched off,
# switched on by name. Included once from tests/CMakeLists.txt.
# The table of the flags and the canonical text.
terrain_add_test(experimental_flags_test terrain_core)
# The planet file of version 2: the flags, then the level, then its sections (internal header).
terrain_add_test(planet_flags_test terrain_core)
target_include_directories(planet_flags_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/storage)
# The world without any flag, that of main at sea and on the coasts.
terrain_add_test(experimental_off_test terrain_core)
# The flags of an Unreal level (Bridge/session_flags.hpp, F2): its lines in the log, its session,
# its planet file refused when of other flags; and ATerrainWorld::Experimental reaching the session
# (the plugin itself is only built by Unreal).
target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/session_flags.cpp)
terrain_add_test(unreal_experimental_flags_test terrain_unreal_bridge)
add_test(NAME unreal_experimental_flags
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/unreal_experimental_flags.cmake)
