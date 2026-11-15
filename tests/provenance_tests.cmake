# The provenance of the planet file (agent 55, task T4 of the audit of agent 53): the fingerprint
# of the relief in the section PROV, the two refusals (other flags; another relief), a file
# written before it read with "provenance: unknown", the old file kept as .bak. Included once from
# tests/CMakeLists.txt, after experimental_flags_tests.cmake and coast_tests.cmake.
terrain_add_test(planet_provenance_test terrain_probe_lib)
target_include_directories(planet_provenance_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/storage)
# The line of the log of the Bridge ("no provenance") and the cost of the fingerprint as a session
# reads the planet file of the fixture coast_planet.
terrain_add_test(unreal_planet_provenance_test terrain_unreal_bridge)
target_include_directories(unreal_planet_provenance_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/storage)
set_tests_properties(unreal_planet_provenance_test PROPERTIES FIXTURES_REQUIRED coast_planet
    ENVIRONMENT TERRAIN_COAST_PLANET=${CMAKE_BINARY_DIR}/coast_planet.bin)
