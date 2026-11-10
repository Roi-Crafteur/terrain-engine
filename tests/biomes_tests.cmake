# The tests of the biomes of the land (agent 49): the format of the blend texels and the water the
# texels of the tiles read. The tests of a task live in a file of their own, included once from
# tests/CMakeLists.txt.

# Every field of a blend texel (ids, water, cover, dunes, weights) back from its texel, none
# spilling into another.
terrain_add_test(biome_format_test terrain_core)

# The texels of the tiles take the water of the core (a dry depression is no ocean, the lakes and
# rivers are water), level 0 of the horizon equal to them: on the default world with its planet
# file (the fixture coast_planet of coast_tests.cmake).
terrain_add_test(biome_tiles_water_test terrain_core)
set_tests_properties(biome_tiles_water_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${CMAKE_BINARY_DIR}/coast_planet.bin)
