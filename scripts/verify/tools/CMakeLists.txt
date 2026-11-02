# The tools of the core a verification needs (terrain_probe or terrain_probe_water before it,
# terrain_make_planet), built from the terrain_core of a version alone: no viewer (GLFW, glad,
# Jinja2), no tests, nothing fetched. scripts/verify/workspace.py configures it with
#   cmake -S scripts/verify/tools -B <cache>/<sha>/tools -DTERRAIN_REPO=<clone of the version>
cmake_minimum_required(VERSION 3.20)
project(TerrainVerifyTools VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TERRAIN_REPO OR NOT EXISTS "${TERRAIN_REPO}/terrain_core/CMakeLists.txt")
    message(FATAL_ERROR "TERRAIN_REPO must be a clone of terrain-engine, not '${TERRAIN_REPO}'")
endif()

# The warnings of the project targets: none required here (the CI checks them).
add_library(terrain_warnings INTERFACE)

add_subdirectory(${TERRAIN_REPO}/terrain_core terrain_core)
