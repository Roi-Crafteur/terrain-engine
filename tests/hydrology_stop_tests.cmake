# The hydrology fast and stoppable (agent 39), included by tests/CMakeLists.txt. The tests that
# compare times run alone: their RUN_SERIAL is set in serial_tests.cmake (agent 42).

# The bits of a sample of the hydrology of the small world, pinned before the work of agent 39.
terrain_add_test(hydrology_bits_test terrain_core)

# The computations of the hydrology stopped from another thread, under load, as two tests of one
# executable: hydrology_stop_test (the threads stopped in the long courses, nothing half made) and
# hydrology_wait_test (a thread waiting for a course another one makes, the others going on).
add_executable(hydrology_stop_test hydrology_stop_test.cpp)
target_link_libraries(hydrology_stop_test PRIVATE terrain_core terrain_warnings)
add_test(NAME hydrology_stop_test COMMAND hydrology_stop_test stops)
add_test(NAME hydrology_wait_test COMMAND hydrology_stop_test waits)
add_dependencies(check hydrology_stop_test)
# A thread waiting in a cache of the hydrology woken by the raise of its flag, not by a step of
# its wait (its step never ends there): no time measured.
terrain_add_test(lazy_wake_test terrain_core)
target_include_directories(lazy_wake_test PRIVATE
                           ${PROJECT_SOURCE_DIR}/terrain_core/src/generation/water)
# The same bits on 1 and 4 threads, and after stops in the middle.
terrain_add_test(hydrology_threads_test terrain_core)

# The threads of the plugin (the dev map, the ground reads, the GPU horizon, the tiles) stopped in
# the middle of the courses of the rivers, one by one and as ReleaseWorld stops them.
terrain_add_test(unreal_work_stop_test terrain_unreal_bridge)

# The caches of the hydrology bounded in bytes: dropped and made again, the same bits.
terrain_add_test(hydrology_cache_test terrain_core)

# The goals in time of the stops (the medians under 100 ms), printed by the tests and checked only
# by these benches, the same sources built with TERRAIN_TIME_GOALS (time_goal_support.hpp, agent
# 42), run by hand on a quiet machine; hydrology_stop_test_bench runs both parts.
add_executable(hydrology_stop_test_bench hydrology_stop_test.cpp)
target_link_libraries(hydrology_stop_test_bench PRIVATE terrain_core terrain_warnings)
add_executable(unreal_work_stop_test_bench unreal_work_stop_test.cpp)
target_link_libraries(unreal_work_stop_test_bench PRIVATE terrain_unreal_bridge terrain_warnings)
foreach(bench hydrology_stop_test_bench unreal_work_stop_test_bench)
    target_compile_definitions(${bench} PRIVATE TERRAIN_TIME_GOALS)
    add_dependencies(check ${bench})
endforeach()
