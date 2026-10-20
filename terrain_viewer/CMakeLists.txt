include(FetchContent)

# GLFW 3.4: use the system package when available, otherwise fetch it.
find_package(glfw3 3.4 CONFIG QUIET)
if(NOT glfw3_FOUND)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "")
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "")
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "")
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "")
    set(GLFW_INSTALL OFF CACHE BOOL "")
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

# glad 2: OpenGL 3.3 core loader, generated from the bundled Khronos specs.
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake
)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl_core_33 STATIC REPRODUCIBLE API gl:core=3.3)

# Camera and math are kept free of OpenGL so they can be unit tested.
add_library(terrain_viewer_camera STATIC
    src/camera.cpp
)
target_include_directories(terrain_viewer_camera PUBLIC src)
target_compile_features(terrain_viewer_camera PUBLIC cxx_std_20)
target_link_libraries(terrain_viewer_camera PRIVATE terrain_warnings)

# The view modes, their names and the choice made by key 5: free of OpenGL and of GLFW, tested.
add_library(terrain_viewer_modes STATIC
    src/view_modes.cpp
)
target_include_directories(terrain_viewer_modes PUBLIC src)
target_compile_features(terrain_viewer_modes PUBLIC cxx_std_20)
target_link_libraries(terrain_viewer_modes PRIVATE terrain_warnings)

# How the caves look (colors, speleothem cones, cave districts and flora), computed on the CPU
# without OpenGL: tested.
add_library(terrain_viewer_cave_look STATIC
    src/cave_look.cpp
    src/district_look.cpp
    src/flora_look.cpp
)
target_include_directories(terrain_viewer_cave_look PUBLIC src)
target_compile_features(terrain_viewer_cave_look PUBLIC cxx_std_20)
target_link_libraries(terrain_viewer_cave_look PUBLIC terrain_core PRIVATE terrain_warnings)

add_executable(terrain_viewer
    src/main.cpp
    src/shader.cpp
    src/cave_world_view.cpp
    src/cube_mesh.cpp
    src/chunk_mesh.cpp
    src/cliff_region_view.cpp
    src/colored_mesh.cpp
    src/demo_chunks.cpp
    src/eroded_region_view.cpp
    src/heightfield_region.cpp
    src/material_mesh.cpp
    src/model_views.cpp
    src/sculpted_region_view.cpp
    src/streamed_world_view.cpp
    src/view_keys.cpp
)
target_compile_definitions(terrain_viewer PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(terrain_viewer PRIVATE
    terrain_core
    terrain_viewer_camera
    terrain_viewer_cave_look
    terrain_viewer_modes
    glad_gl_core_33
    glfw
    terrain_warnings
)
