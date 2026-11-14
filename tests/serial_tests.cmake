# The tests that run alone under ctest --parallel (agent 42), included once, at the end of
# tests/CMakeLists.txt, after every test is defined.
#
# RUN_SERIAL: the tests whose verdict depends on the time they take or on the threads being
# scheduled in time: a threshold in milliseconds or seconds, a ratio of measured times, a sleep
# before counting what started. Found by a search of the clocks in tests/ and
# scripts/verify/tests/ (steady_clock, chrono::, sleep_for, secondsOf, ratioAtMost, time. in
# Python), then by reading each assertion. A test that only prints its times, waits at least 5 s
# for a condition, or runs on a simulated clock may share the machine. A new test that measures
# a time is added here.
#
# Their durations stay comparable from one CI to the next; those of the other tests include the
# cores they shared.

set_tests_properties(
    # A ratio of measured times.
    water_cost_test
    ergs_world_test
    horizon_edits_cost_test
    unreal_horizon_edits_cost_test
    unreal_terrain_session_test
    # A threshold on a measured time.
    unreal_map_cache_test
    # A stop: a job dropped while it builds.
    unreal_planet_job_test
    # A wait bounded in time, or a sleep before counting what the threads did.
    unreal_edit_latency_test
    unreal_edit_thread_test
    unreal_tile_streamer_test
    unreal_point_info_test
    map_render_test
    # A reader holding a file for 60 ms against the retries of a save (Windows).
    modifications_test
    world_save_test
    PROPERTIES RUN_SERIAL TRUE)

# The watchdog of the verification launcher on real processes: 3 s to start, 1 s between two
# beats (only with Python).
if(TEST verify_session_test)
    set_tests_properties(verify_session_test PROPERTIES RUN_SERIAL TRUE)
endif()

# The tests of a file included after this one (its line comes later in tests/CMakeLists.txt): set
# at the end of the folder, once every test is defined, and only if the test exists.
function(terrain_run_serial_if_defined)
    foreach(name IN LISTS ARGN)
        if(TEST ${name})
            set_tests_properties(${name} PROPERTIES RUN_SERIAL TRUE)
        endif()
    endforeach()
endfunction()
cmake_language(DEFER CALL terrain_run_serial_if_defined
    # A stop (agent 39, tests/hydrology_stop_tests.cmake): the computations of the hydrology
    # stopped from another thread, under load; a thread waiting for a course another one makes,
    # stopped; the threads of the plugin stopped in the middle of the courses of the rivers.
    hydrology_stop_test
    hydrology_wait_test
    unreal_work_stop_test)

# The planet of the default world, built by the fixture of the coast tests (two minutes on four
# cores): started first, so that the tests it holds back are not left for the end.
set_tests_properties(coast_planet_file PROPERTIES COST 1000)
