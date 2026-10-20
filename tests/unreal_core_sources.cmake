cmake_minimum_required(VERSION 3.20)

# The Unreal plugin compiles the sources of terrain_core through one wrapper each, in
# terrain_unreal/Source/TerrainEngine/Private/TerrainCore: core_<dir>_<name>.cpp for
# terrain_core/src/<dir>/<name>.cpp (core_<name>.cpp at the root of src), which includes it by
# its relative path. Every source outside io/ (PNG exports for checks, unused in game) must have
# its wrapper, and no wrapper may be left for a removed source.
# Usage: cmake -DROOT=<repository> -P unreal_core_sources.cmake

set(core_src "${ROOT}/terrain_core/src")
set(wrappers_dir "${ROOT}/terrain_unreal/Source/TerrainEngine/Private/TerrainCore")

file(GLOB_RECURSE sources RELATIVE "${core_src}" "${core_src}/*.cpp")
list(FILTER sources EXCLUDE REGEX "^io/")
list(SORT sources)
file(GLOB wrappers RELATIVE "${wrappers_dir}" "${wrappers_dir}/*.cpp")

set(errors "")
set(expected_wrappers "")
foreach(source IN LISTS sources)
    string(REPLACE "/" "_" flat "${source}")
    set(wrapper "core_${flat}")
    list(APPEND expected_wrappers "${wrapper}")
    if(NOT EXISTS "${wrappers_dir}/${wrapper}")
        string(APPEND errors "missing wrapper ${wrapper} for terrain_core/src/${source}\n")
        continue()
    endif()
    file(READ "${wrappers_dir}/${wrapper}" content)
    string(FIND "${content}" "#include \"../../../../../terrain_core/src/${source}\"" found)
    if(found EQUAL -1)
        string(APPEND errors "${wrapper} does not include terrain_core/src/${source}\n")
    endif()
endforeach()

foreach(wrapper IN LISTS wrappers)
    if(NOT wrapper IN_LIST expected_wrappers)
        string(APPEND errors "wrapper ${wrapper} matches no source of terrain_core outside io/\n")
    endif()
endforeach()

if(NOT errors STREQUAL "")
    message(FATAL_ERROR "terrain_core sources and Unreal wrappers differ:\n${errors}")
endif()
list(LENGTH sources count)
message(STATUS "${count} terrain_core sources, each with its Unreal wrapper")
