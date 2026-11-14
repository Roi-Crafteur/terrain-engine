# The land below the sea level in the materials of Unreal (agent 52, PR 52a-bis): the sea near a
# pixel, read by the wet rock and by the depth offset of the GPU horizon instead of the altitude.
# Included once from tests/CMakeLists.txt, after coast_tests.cmake (its planet file).
terrain_add_test(lows_sea_near_test terrain_unreal_bridge)
set_tests_properties(lows_sea_near_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${coast_planet_file})
find_package(Python COMPONENTS Interpreter QUIET)
if(Python_Interpreter_FOUND)
    add_test(NAME lows_shader_scripts_test
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/lows_shader_scripts_test.py
                ${PROJECT_SOURCE_DIR}/terrain_unreal/Scripts
                ${CMAKE_CURRENT_SOURCE_DIR}/lows_sea_near_test.cpp)
endif()
