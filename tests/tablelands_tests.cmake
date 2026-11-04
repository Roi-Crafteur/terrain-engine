# The plateaus, mesas, karst and towers (agent 46, tablelands.hpp): the provinces from the geology
# and the pre-climate (pre_climate.hpp, shared with agent 52), the climate they read against that
# of the planet level. Included once from tests/CMakeLists.txt.
terrain_add_test(tablelands_map_test terrain_core)
# The pre-climate against the climate of the planet level of the default world (the planet file
# of the fixture coast_planet, tests/coast_tests.cmake).
terrain_add_test(pre_climate_test terrain_core)
set_tests_properties(pre_climate_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${coast_planet_file})
