# Pure generation library: must never depend on OpenGL, GLFW, glad or a window.
add_library(terrain_core STATIC
    src/chunk_coord.cpp
)

target_include_directories(terrain_core PUBLIC include)
target_compile_features(terrain_core PUBLIC cxx_std_20)
target_link_libraries(terrain_core PRIVATE terrain_warnings)
