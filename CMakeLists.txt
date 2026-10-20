cmake_minimum_required(VERSION 3.20)

project(TerrainEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TERRAIN_WARNINGS_AS_ERRORS "Treat warnings of the project targets as errors (CI)" OFF)

# Warnings applied to project targets only, never to fetched dependencies.
add_library(terrain_warnings INTERFACE)
if(MSVC)
    target_compile_options(terrain_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(terrain_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
# Off by default; the CI turns it on so that any warning fails the build.
if(TERRAIN_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(terrain_warnings INTERFACE /WX)
    else()
        target_compile_options(terrain_warnings INTERFACE -Werror)
    endif()
endif()

enable_testing()

add_subdirectory(terrain_core)
add_subdirectory(terrain_viewer)
add_subdirectory(tests)
