# The tests of the biomes of the land (agent 49): the format of the blend texels and the water the
# texels of the tiles read. The tests of a task live in a file of their own, included once from
# tests/CMakeLists.txt.

# Every field of a blend texel (ids, water, cover, dunes, weights) back from its texel, none
# spilling into another.
terrain_add_test(biome_format_test terrain_core)

# The humidity the biomes read from the planet level, at a warped point: no fold of its bilinear
# grid of 8 km left on the lines of the cell centers (the small planet).
terrain_add_test(biome_humidity_test terrain_core)

# The texels of the tiles take the water of the core (a dry depression is no ocean, the lakes and
# rivers are water), level 0 of the horizon equal to them: on the default world with its planet
# file (the fixture coast_planet of coast_tests.cmake).
terrain_add_test(biome_tiles_water_test terrain_core)
set_tests_properties(biome_tiles_water_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${CMAKE_BINARY_DIR}/coast_planet.bin)

# The share of every biome of the land on the real planet (the table of the report of agent 49),
# every biome present, and the soils of its deserts: the fixture coast_planet.
terrain_add_test(biome_planet_test terrain_core)
set_tests_properties(biome_planet_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${CMAKE_BINARY_DIR}/coast_planet.bin)

# The grass of a desert within 150 m of a river on its way to the screen: in the texels of the
# tiles, the samples of the horizon (levels 0 to 5) and the channels of the GPU (the fixture
# coast_planet).
terrain_add_test(biome_desert_river_test terrain_unreal_bridge)
set_tests_properties(biome_desert_river_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${CMAKE_BINARY_DIR}/coast_planet.bin)

# Its cost, paired with and without the distance to the rivers: tiles along a river of a desert,
# tiles away from the deserts (the fixture coast_planet).
terrain_add_test(biome_cost_test terrain_core)
set_tests_properties(biome_cost_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${CMAKE_BINARY_DIR}/coast_planet.bin)

# The biomes of the dev map over the real planet: the sand desert at 200 m and 4 km per pixel,
# never a course of the hydrology (the guard of map_cost_test; the fixture coast_planet).
terrain_add_test(biome_map_test terrain_core)
set_tests_properties(biome_map_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${CMAKE_BINARY_DIR}/coast_planet.bin)
