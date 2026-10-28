# The tests of the planet level created in little memory (phase 11D), included by
# tests/CMakeLists.txt.

# Compact arrays of the planet level: land index, values of the land, values of a few cells.
terrain_add_test(planet_land_test terrain_core)

# The blurs of the climate in place and in flux: the bits of the passes on whole grids.
terrain_add_test(planet_blur_test terrain_core)
target_include_directories(planet_blur_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation/planet)

# The lean build of the planet level on the small world: its level the golden one, its heap at
# the peak within a bound proportional to the grid (its own operator new counts the heap).
terrain_add_test(planet_memory_test terrain_core)
target_sources(planet_memory_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/tools/heap_counter.cpp)
target_include_directories(planet_memory_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/tools)
