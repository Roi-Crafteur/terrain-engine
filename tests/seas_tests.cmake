# The seas (agent 51): the figures of their floors beside the bathymetry of the Earth. Included
# once from tests/CMakeLists.txt.
# F1: the experimental flags of the world, in the planet file, and the world without any flag
# that of main, bit for bit.
terrain_add_test(experimental_flags_test terrain_core)
terrain_add_test(planet_flags_test terrain_core)
terrain_add_test(seas_off_test terrain_core)
terrain_add_test(seas_bathymetry_test terrain_core)
terrain_add_test(sea_floor_test terrain_core)
target_include_directories(sea_floor_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(seas_world_test terrain_core)
terrain_add_test(seas_cost_test terrain_core)
set_tests_properties(seas_cost_test PROPERTIES RUN_SERIAL TRUE)
terrain_add_test(sea_plates_test terrain_core)
target_include_directories(sea_plates_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(sea_hills_test terrain_core)
target_include_directories(sea_hills_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(seas_seam_test terrain_core)
# The places of the coasts of agent 33 with the seas on and off, on the planet of the fixture
# coast_planet (tests/coast_tests.cmake).
add_executable(seas_coast_places_test seas_coast_places_test.cpp)
target_link_libraries(seas_coast_places_test PRIVATE terrain_core terrain_warnings)
add_test(NAME seas_coast_places_test
    COMMAND seas_coast_places_test ${PROJECT_SOURCE_DIR}/data/dev/places.json)
add_dependencies(check seas_coast_places_test)
set_tests_properties(seas_coast_places_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${coast_planet_file})
