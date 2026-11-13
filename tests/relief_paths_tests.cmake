# The paths of the relief compared bit for bit (agent 55, task T3 of the audit of agent 53): every
# copy of the chain of sampling (sampleAt, the batches, seenAt, a cell of the far horizon, the
# columns and texels of the LOD 3 tiles) on the default world with its planet file (the fixture
# coast_planet of tests/coast_tests.cmake). A merge that touches heightfield*.cpp,
# horizon_sampler.cpp, tile_blend.cpp or terrain_surface.cpp runs it. Included once from
# tests/CMakeLists.txt.
terrain_add_test(relief_paths_test terrain_core)
target_include_directories(relief_paths_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/streaming)
set_tests_properties(relief_paths_test PROPERTIES FIXTURES_REQUIRED coast_planet
    ENVIRONMENT TERRAIN_COAST_PLANET=${CMAKE_BINARY_DIR}/coast_planet.bin)
# The render section of terrain_probe reads the path of the far horizon (forFootprint(8).seenAt).
terrain_add_test(probe_render_test terrain_probe_lib)
