# The plateaus, mesas, karst and towers (agent 46, tablelands.hpp): the provinces from the geology
# and the pre-climate (pre_climate.hpp, shared with agent 52), the climate they read against that
# of the planet level. Included once from tests/CMakeLists.txt.
terrain_add_test(tablelands_map_test terrain_core)
target_include_directories(tablelands_map_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
# The pre-climate against the climate of the planet level of the default world (the planet file
# of the fixture coast_planet, tests/coast_tests.cmake).
terrain_add_test(pre_climate_test terrain_core)
set_tests_properties(pre_climate_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${coast_planet_file})
# The world with them switched on: its checksums, and what it does to the frozen mountains (R1).
terrain_add_test(tablelands_on_golden_test terrain_core)
# The rows "tablelands" and "karst" of the experimental flags (agent 46's copy of the mechanism of
# agent 51: they go into its experimental_flags_test when its pull request is on main).
terrain_add_test(tablelands_flags_test terrain_core)
# The goldens with the plateaus on (golden_support.hpp: the same executables run with --flags
# tablelands), their values pinned for R1, which turns every new relief on together. Only those the
# high surfaces of the plateaus reach: the horizon, the streamed world and the small world of the
# planet level keep the values of main with the flag.
foreach(golden heightfield_golden_test coast_outside_test mountain_freeze_test)
    add_test(NAME ${golden}_tablelands COMMAND ${golden} --flags tablelands)
endforeach()
