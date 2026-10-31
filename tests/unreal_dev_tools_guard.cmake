cmake_minimum_required(VERSION 3.20)

# The tools of the developers (agent 36) are never in a Shipping build: TerrainEngine.Build.cs
# defines TERRAIN_DEV_TOOLS to 0 there and to 1 elsewhere, every file of the tools holds all its
# code (console commands and variables included) under #if TERRAIN_DEV_TOOLS, the hooks of
# ATerrainWorld and the looks of the tiles are empty without it, and no other file of the plugin
# registers a command of the tools. The plugin is only ever built by Unreal: this reads its
# sources.
# Usage: cmake -DROOT=<repository> -P unreal_dev_tools_guard.cmake

set(plugin "${ROOT}/terrain_unreal/Source/TerrainEngine")
set(errors "")

file(READ "${plugin}/TerrainEngine.Build.cs" build)
string(FIND "${build}" "Target.Configuration != UnrealTargetConfiguration.Shipping" shipping)
string(FIND "${build}" "bDevTools ? \"TERRAIN_DEV_TOOLS=1\" : \"TERRAIN_DEV_TOOLS=0\"" define)
if(shipping EQUAL -1 OR define EQUAL -1)
    string(APPEND errors "TerrainEngine.Build.cs: TERRAIN_DEV_TOOLS is not 0 in Shipping\n")
endif()

# Every file of the tools: its code between "#if TERRAIN_DEV_TOOLS" (after its own header) and
# the "#endif // TERRAIN_DEV_TOOLS" that ends it.
set(tools TerrainDevCommands.cpp TerrainDevLooks.cpp TerrainDevScreen.cpp TerrainDevSeed.cpp
          TerrainDevShots.cpp TerrainDevTools.cpp TerrainDevView.cpp TerrainDevState.h)
foreach(name IN LISTS tools)
    file(READ "${plugin}/Private/${name}" text)
    string(REGEX REPLACE "\r" "" text "${text}")
    string(FIND "${text}" "#if TERRAIN_DEV_TOOLS\n" open)
    string(FIND "${text}" "#endif // TERRAIN_DEV_TOOLS" close REVERSE)
    string(LENGTH "${text}" length)
    string(SUBSTRING "${text}" 0 ${open} before)
    string(REGEX MATCHALL "\n[^/\n#][^\n]*" code "\n${before}")
    math(EXPR tail "${length} - ${close}")
    if(open EQUAL -1 OR close EQUAL -1 OR tail GREATER 30 OR NOT code STREQUAL "")
        string(APPEND errors "${name}: its code is not all under #if TERRAIN_DEV_TOOLS\n")
    endif()
endforeach()

# The hooks of ATerrainWorld and the looks of the tiles exist in every build, empty without it.
foreach(name TerrainWorldDev.cpp ProceduralMeshTerrainLooks.cpp)
    file(READ "${plugin}/Private/${name}" text)
    string(REGEX MATCHALL "#if TERRAIN_DEV_TOOLS" guards "${text}")
    list(LENGTH guards count)
    if(count LESS 3)
        string(APPEND errors "${name}: its bodies are not under #if TERRAIN_DEV_TOOLS\n")
    endif()
endforeach()

# No other file registers a command or a variable of the tools.
file(GLOB sources "${plugin}/Private/*.cpp" "${plugin}/Private/*.h" "${plugin}/Public/*.h")
foreach(file IN LISTS sources)
    get_filename_component(name "${file}" NAME)
    if(name IN_LIST tools)
        continue()
    endif()
    file(READ "${file}" text)
    foreach(command Terrain.Goto Terrain.Look Terrain.Eye Terrain.Pawn Terrain.Save Terrain.Info
                    Terrain.Shot Terrain.Views Terrain.View\" Terrain.Seed Terrain.Hud
                    Terrain.PlacesFile Terrain.ShotTimeout Terrain.ShotSettle)
        string(FIND "${text}" "TEXT(\"${command}" at)
        if(NOT at EQUAL -1)
            string(APPEND errors "${name}: registers ${command} outside the files of the tools\n")
        endif()
    endforeach()
endforeach()

if(NOT errors STREQUAL "")
    message(FATAL_ERROR "the tools of the developers could reach a Shipping build:\n${errors}")
endif()
message(STATUS "the tools of the developers are all under TERRAIN_DEV_TOOLS, 0 in Shipping")
