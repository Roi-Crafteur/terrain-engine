# The hydrology fast and stoppable (agent 39), included by tests/CMakeLists.txt.

# The bits of a sample of the hydrology of the small world, pinned before the work of agent 39.
terrain_add_test(hydrology_bits_test terrain_core)

# The computations of the hydrology stopped from another thread, under load: they measure their
# latencies, so they run alone.
terrain_add_test(hydrology_stop_test terrain_core)
set_tests_properties(hydrology_stop_test PROPERTIES RUN_SERIAL TRUE)
# The same bits on 1 and 4 threads, and after stops in the middle.
terrain_add_test(hydrology_threads_test terrain_core)

# The threads of the plugin (the dev map, the ground reads, the GPU horizon, the tiles) stopped in
# the middle of the courses of the rivers, one by one and as ReleaseWorld stops them: they measure
# their latencies, so they run alone.
terrain_add_test(unreal_work_stop_test terrain_unreal_bridge)
set_tests_properties(unreal_work_stop_test PROPERTIES RUN_SERIAL TRUE)

# The caches of the hydrology bounded in bytes: dropped and made again, the same bits.
terrain_add_test(hydrology_cache_test terrain_core)
