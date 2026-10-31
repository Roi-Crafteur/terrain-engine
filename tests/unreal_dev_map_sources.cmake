# The Unreal side of the dev map (agent 35), which only Unreal compiles: its modules, its component
# created by ATerrainWorld and stopped before the world it reads goes away, and the guard of an
# error of the first compilation of the tools of agent 36 (a TObjectPtr given where a raw pointer is
# expected: the input component of the map is always handed over with .Get()).
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
string(FIND "${source}" "DevMap->StopMap();" stop)
string(FIND "${source}" "State.Reset(); // the workers stop" reset)
if(stop EQUAL -1 OR reset EQUAL -1 OR NOT stop LESS reset)
    message(FATAL_ERROR "TerrainWorld.cpp: the map must stop before the session goes (EndPlay)")
endif()
if(actions MATCHES "InputComponent\\(MapInput\\)")
    message(FATAL_ERROR "TerrainDevMapActions.cpp: MapInput handed over without .Get()")
endif()
message(STATUS "the dev map: its modules, its component, stopped before the session")
