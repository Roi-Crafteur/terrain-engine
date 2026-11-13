cmake_minimum_required(VERSION 3.20)

# The experimental flags of an Unreal level reach its session (agent 51): ATerrainWorld declares
# Experimental, an FTerrainExperimentalSettings (TerrainExperimentalSettings.h) edited in the
# category Terrain|Experimental, whose FString ExperimentalFlags MakeSessionParams hands to
# SessionParams::experimental in UTF-8; StartStreaming writes the lines of the flags in the log,
# a warning per unknown name (session_flags.hpp, unreal_experimental_flags_test); Terrain.Seed
# gives them to its planet job; TerrainWorld.h stays within 500 lines. The plugin is only ever
# built by Unreal: this reads its sources.
# Usage: cmake -DROOT=<repository> -P unreal_experimental_flags.cmake

set(plugin "${ROOT}/terrain_unreal/Source/TerrainEngine")
file(READ "${plugin}/Public/TerrainWorld.h" header)
file(READ "${plugin}/Public/TerrainExperimentalSettings.h" settings)
file(READ "${plugin}/Private/TerrainWorld.cpp" source)
file(READ "${plugin}/Private/TerrainDevSeed.cpp" seed)
# Its lines, counted by their ends (file(STRINGS) would split them at non-ASCII bytes).
string(REGEX MATCHALL "\n" ends "${header}")
list(LENGTH ends count)

set(errors "")
string(REGEX MATCH
    "UPROPERTY\\(EditAnywhere, BlueprintReadOnly, Category = \"Terrain\\|Experimental\"\\)[ \t\r\n]+FTerrainExperimentalSettings Experimental;"
    property "${header}")
if(property STREQUAL "")
    string(APPEND errors "TerrainWorld.h: no FTerrainExperimentalSettings Experimental edited in "
                         "Terrain|Experimental\n")
endif()
string(FIND "${settings}" "FString ExperimentalFlags;" flags)
if(flags EQUAL -1)
    string(APPEND errors "TerrainExperimentalSettings.h: no FString ExperimentalFlags\n")
endif()
string(FIND "${source}" "Params.experimental = TCHAR_TO_UTF8(*World.Experimental.ExperimentalFlags);" handed)
string(FIND "${source}" "terrain::unreal::flagLines(" logged)
string(FIND "${source}" "LogExperimentalFlags(Params.experimental);" started)
if(handed EQUAL -1 OR logged EQUAL -1 OR started EQUAL -1)
    string(APPEND errors "TerrainWorld.cpp: MakeSessionParams does not hand the flags over, or "
                         "StartStreaming does not log them\n")
endif()
string(FIND "${seed}" "TerrainDev::ToUtf8(World->Experimental.ExperimentalFlags)" job)
if(job EQUAL -1)
    string(APPEND errors "TerrainDevSeed.cpp: the planet job of Terrain.Seed has no flags\n")
endif()
if(count GREATER 500)
    string(APPEND errors "TerrainWorld.h: ${count} lines, more than 500\n")
endif()

if(NOT errors STREQUAL "")
    message(FATAL_ERROR "the experimental flags of ATerrainWorld do not reach its session:\n${errors}")
endif()
message(STATUS "ATerrainWorld::Experimental.ExperimentalFlags reaches SessionParams::experimental, "
               "logged at the start, given to Terrain.Seed; TerrainWorld.h: ${count} lines")
