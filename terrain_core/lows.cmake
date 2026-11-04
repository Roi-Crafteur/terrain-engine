# The land below the sea level (agent 52): included once from terrain_core/CMakeLists.txt.

# Measures and a map of the land below the sea level of a world, its closed lows by the climate
# belts of the planet level, what the readers make of it (see the top of tools/export_lows.cpp):
# terrain_export_lows <output-dir> [--planet <file>] [--seed <seed>] [--world <x> <z>]
#                     [--threads <n>] [--cell <meters>] [--width <pixels>] [--readers <n>] [--list]
add_executable(terrain_export_lows tools/export_lows.cpp tools/lows_world.cpp
                                   tools/lows_sites.cpp tools/lows_readers.cpp)
target_link_libraries(terrain_export_lows PRIVATE terrain_core terrain_warnings)

# The sources of the lows in terrain_core (their Unreal sources: TerrainCore/core_generation_*).
target_sources(terrain_core PRIVATE
    src/generation/low_basins.cpp
    src/generation/low_reach.cpp
    src/generation/low_sites.cpp
    src/generation/rift_grabens.cpp)
