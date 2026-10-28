cmake_minimum_required(VERSION 3.20)

# The planet file of an Unreal level reaches its session (task 7b): ATerrainWorld declares
# PlanetFile, an FString edited in the category Terrain|World, and MakeSessionParams hands it to
# SessionParams::planetFile in UTF-8 (unreal_terrain_session_test opens a planet file from such a
# path, in a folder whose name is not ASCII). The plugin is only ever built by Unreal: this reads
# its sources.
# Usage: cmake -DROOT=<repository> -P unreal_planet_file.cmake

set(plugin "${ROOT}/terrain_unreal/Source/TerrainEngine")
file(READ "${plugin}/Public/TerrainWorld.h" header)
file(READ "${plugin}/Private/TerrainWorld.cpp" source)

set(errors "")
string(REGEX MATCH
    "UPROPERTY\\(EditAnywhere, BlueprintReadOnly, Category = \"Terrain\\|World\"\\)[ \t\r\n]+FString PlanetFile;"
    property "${header}")
if(property STREQUAL "")
    string(APPEND errors "TerrainWorld.h: no FString PlanetFile edited in Terrain|World\n")
endif()
string(FIND "${source}" "Params.planetFile = TCHAR_TO_UTF8(*World.PlanetFile);" handed)
if(handed EQUAL -1)
    string(APPEND errors "TerrainWorld.cpp: MakeSessionParams does not hand PlanetFile over\n")
endif()

if(NOT errors STREQUAL "")
    message(FATAL_ERROR "the planet file of ATerrainWorld does not reach its session:\n${errors}")
endif()
message(STATUS "ATerrainWorld::PlanetFile reaches SessionParams::planetFile in UTF-8")
