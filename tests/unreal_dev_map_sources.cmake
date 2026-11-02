# The Unreal side of the dev map (agent 35), which only Unreal compiles: its modules, its component
# created by ATerrainWorld and stopped before the world it reads goes away (ReleaseWorld: at the
# end of the play and before Terrain.Seed builds another world), the full-screen map, its places
# and its clicks never in Shipping (all their code under #if TERRAIN_DEV_TOOLS, as the tools of
# agent 36; the minimap in every build), and the guard of an error of the first compilation of the
# tools of agent 36 (a TObjectPtr given where a raw pointer is expected: the input component of the
# map is always handed over with .Get()).
# Usage: cmake -DROOT=<repository> -P unreal_dev_map_sources.cmake

set(plugin "${ROOT}/terrain_unreal/Source/TerrainEngine")
file(READ "${plugin}/TerrainEngine.Build.cs" build)
file(READ "${plugin}/Public/TerrainWorld.h" header)
file(READ "${plugin}/Private/TerrainWorld.cpp" source)
file(READ "${plugin}/Private/TerrainDevMapActions.cpp" actions)

foreach(module Slate SlateCore InputCore)
    if(NOT build MATCHES "\"${module}\"")
        message(FATAL_ERROR "TerrainEngine.Build.cs: the module ${module} of the map is missing")
    endif()
endforeach()
if(NOT header MATCHES "TObjectPtr<UTerrainDevMapComponent> DevMap;"
   OR NOT header MATCHES "friend class UTerrainDevMapComponent;")
    message(FATAL_ERROR "TerrainWorld.h: the map component (DevMap) or its access to the state")
endif()
if(NOT source MATCHES "DevMap = CreateDefaultSubobject<UTerrainDevMapComponent>")
    message(FATAL_ERROR "TerrainWorld.cpp: ATerrainWorld does not create its map component")
endif()
string(FIND "${source}" "void ATerrainWorld::ReleaseWorld()" release)
string(FIND "${source}" "DevMap->StopMap();" stop)
string(FIND "${source}" "State.Reset(); // the workers stop" reset)
if(release EQUAL -1 OR stop EQUAL -1 OR reset EQUAL -1 OR NOT release LESS stop
   OR NOT stop LESS reset)
    message(FATAL_ERROR "TerrainWorld.cpp: the map must stop in ReleaseWorld, before the session goes")
endif()

# The files of the full-screen map: all their code between "#if TERRAIN_DEV_TOOLS" (after their own
# header) and the "#endif // TERRAIN_DEV_TOOLS" that ends them.
foreach(name TerrainDevMapLanding.cpp TerrainDevMapPlaces.cpp STerrainDevMap.cpp STerrainDevMap.h)
    file(READ "${plugin}/Private/${name}" text)
    string(REGEX REPLACE "\r" "" text "${text}")
    string(FIND "${text}" "#if TERRAIN_DEV_TOOLS\n" open)
    string(FIND "${text}" "#endif // TERRAIN_DEV_TOOLS" close REVERSE)
    string(LENGTH "${text}" length)
    if(open EQUAL -1 OR close EQUAL -1)
        message(FATAL_ERROR "${name}: its code is not under #if TERRAIN_DEV_TOOLS")
    endif()
    string(SUBSTRING "${text}" 0 ${open} before)
    string(REGEX MATCHALL "\n[^/\n#][^\n]*" code "\n${before}")
    math(EXPR tail "${length} - ${close}")
    if(tail GREATER 30 OR NOT code STREQUAL "")
        message(FATAL_ERROR "${name}: its code is not all under #if TERRAIN_DEV_TOOLS")
    endif()
endforeach()
# The other files: the full-screen map, its key and Terrain.Map under the guard, no other one.
file(READ "${plugin}/Private/TerrainDevMap.cpp" component)
file(READ "${plugin}/Private/TerrainDevMapCommands.cpp" commands)
foreach(name actions component commands)
    if(${name} MATCHES "UE_BUILD_SHIPPING" OR NOT ${name} MATCHES "#if TERRAIN_DEV_TOOLS")
        message(FATAL_ERROR "${name}: the full-screen map is not under TERRAIN_DEV_TOOLS")
    endif()
endforeach()
string(FIND "${actions}" "#if TERRAIN_DEV_TOOLS\n\tInput->BindKey(DevMapKey" key)
string(FIND "${commands}" "#if TERRAIN_DEV_TOOLS\n\tFAutoConsoleCommandWithWorldAndArgs MapCommand" map)
string(FIND "${commands}" "MapGoCommand(TEXT(\"Terrain.Map.Go\")" go)
string(SUBSTRING "${commands}" ${map} -1 after_map)
string(FIND "${after_map}" "#endif" map_end)
string(FIND "${after_map}" "MapGoCommand(TEXT(\"Terrain.Map.Go\")" go_in)
string(FIND "${actions}" "#if TERRAIN_DEV_TOOLS\n\nnamespace" open_actions)
string(FIND "${actions}" "void UTerrainDevMapComponent::OpenDevMap()" open_map)
string(FIND "${actions}" "void UTerrainDevMapComponent::ClickDevMap(" click)
if(key EQUAL -1 OR map EQUAL -1 OR open_actions EQUAL -1 OR NOT open_actions LESS open_map
   OR NOT open_actions LESS click)
    message(FATAL_ERROR "the key M, Terrain.Map or the full-screen map outside TERRAIN_DEV_TOOLS")
endif()
if(go EQUAL -1 OR go_in EQUAL -1 OR NOT go_in LESS map_end)
    message(FATAL_ERROR "TerrainDevMapCommands.cpp: Terrain.Map.Go outside TERRAIN_DEV_TOOLS")
endif()
if(actions MATCHES "InputComponent\\(MapInput\\)")
    message(FATAL_ERROR "TerrainDevMapActions.cpp: MapInput handed over without .Get()")
endif()
message(STATUS "the dev map: its modules, its component, stopped before the session, the "
               "full-screen map under TERRAIN_DEV_TOOLS")
