# The guards of the goldens and of the tests (agent 42): the world as the game plays it pinned bit
# for bit, the checksum of its planet level checked, not only printed. Included once, at the end
# of tests/CMakeLists.txt (after coast_tests.cmake: the fixture coast_planet and its file).

# The planet level of the default world, as the fixture coast_planet_file reads or builds it
# (terrain_probe --planet): its checksum is the one pinned here. Pinned at ab574e3 (agent 42):
# the planet level of kGeneratorVersion 9, kReliefGeneratorVersion 4. With a pass expression,
# ctest decides on the checksum and no longer on the exit code of terrain_probe: the golden of the
# played world (below) reads the same file and checks the same checksum with its exit code.
set_tests_properties(coast_planet_file PROPERTIES
    PASS_REGULAR_EXPRESSION "\"checksum\": \"0x26626564570b0d9e\"")

# The world as the game plays it (TerrainSession of the Bridge on the planet file of the fixture):
# the tiles of LOD 0 to 3 and the far horizon at five places, and the checksum of the planet level,
# bit for bit (played_world_golden_test.cpp).
terrain_add_test(played_world_golden_test terrain_unreal_bridge)
set_tests_properties(played_world_golden_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${coast_planet_file})

# Benches run by hand, not tests: they assert nothing and print times (each source says how to read
# them). horizon_cost_bench and horizon_blend_cost_bench were horizon_cost_test and
# horizon_blend_cost_test; map_cost_bench holds the times map_cost_test printed. The coverage of
# the horizon they checked is asserted by horizon_levels_test.
foreach(bench horizon_cost_bench horizon_blend_cost_bench map_cost_bench)
    add_executable(${bench} ${bench}.cpp)
    target_link_libraries(${bench} PRIVATE terrain_core terrain_warnings)
    add_dependencies(check ${bench})
endforeach()

# The tests whose time was an absolute limit: each now proves the behavior it protects (a stop, an
# order) or compares two times it measures itself, alone on the machine (RUN_SERIAL, set in
# serial_tests.cmake). Their goals in time are printed by the test and checked only by a bench
# built from the same source with TERRAIN_TIME_GOALS (time_goal_support.hpp), run by hand on a
# quiet machine: <test>_bench. The goal of the Python watchdog:
# TERRAIN_TIME_GOALS=1 python -m unittest test_session.
foreach(timed horizon_edits_cost_test unreal_horizon_edits_cost_test unreal_planet_job_test
        unreal_terrain_session_test)
    add_executable(${timed}_bench ${timed}.cpp)
    target_link_libraries(${timed}_bench PRIVATE terrain_unreal_bridge terrain_warnings)
    target_compile_definitions(${timed}_bench PRIVATE TERRAIN_TIME_GOALS)
    add_dependencies(check ${timed}_bench)
endforeach()

# The rules of the project in report mode (scripts/rules_report.py): the files too long that the
# blocking check leaves out and the functions too long, C++ and Python, reported as warnings by
# scripts/check_project_rules.py, never failing it; its detection of the functions, tested.
find_package(Python COMPONENTS Interpreter QUIET)
if(Python_Interpreter_FOUND)
    add_test(NAME project_rules_test
        COMMAND ${Python_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/project_rules_test.py
                ${PROJECT_SOURCE_DIR}/scripts)
endif()
