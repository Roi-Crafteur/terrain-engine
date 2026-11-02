# The water drawn in Unreal at its level (agent 27): the tests of the water data of the tiles
# (core) and of the water meshes and textures of the Bridge (the probe of the water became
# terrain_probe: tests/verify_tests.cmake). Included once, at the end of tests/CMakeLists.txt
# (after the Bridge library), so that its list stays under 500 lines.

# The water of the columns of the tiles and their falls (core): the columns of the core bit for
# bit, the same on common sides, the flow down the rivers, the falls in the tile of their lip.
terrain_add_test(tile_rivers_test terrain_core)

# The water of the tiles as Unreal draws it (Bridge): the surface of the lakes and rivers, the
# curtains of the falls; their sides against another tile, a coarser one and the horizon.
target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/fall_curtains.cpp
                                             ${unreal_bridge}/fall_edges.cpp
                                             ${unreal_bridge}/fall_foam.cpp
                                             ${unreal_bridge}/planet_source.cpp
                                             ${unreal_bridge}/tile_water_mesh.cpp)
terrain_add_test(unreal_tile_water_test terrain_unreal_bridge)

# The water of the falls of the small world around their lips as Unreal draws it (LOD 0 to 2): no
# hole seen from above in the cut cells, no cell drawn faded out, the foam in its pool.
terrain_add_test(unreal_fall_lips_test terrain_unreal_bridge)

# The curtains of the falls of the small world as Unreal draws them: one piece each, over the
# width of the water falling from their lip, never over the walls of their slot at their ends.
terrain_add_test(unreal_fall_curtains_test terrain_unreal_bridge)

# The pools below the falls of the small world as Unreal draws them: still, never a bump a little
# above the water below their fall; their foam clear of the edge of the next lip, one radius in
# every tile.
terrain_add_test(unreal_fall_pools_test terrain_unreal_bridge)

# The scripts of the materials of the water, without Unreal: they compile, their HLSL declares
# what it reads and writes, the materials they rebuild get every setting reset.
find_package(Python COMPONENTS Interpreter QUIET)
if(Python_Interpreter_FOUND)
    add_test(NAME unreal_water_scripts_test
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unreal_water_scripts_test.py
                ${PROJECT_SOURCE_DIR}/terrain_unreal/Scripts)
    # The foam of the water run as the material computes it: a steep ramp stays white, its
    # streaks keep their scale where the flow turns, the foam of a fall fades out at its border.
    add_test(NAME unreal_water_foam_test
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unreal_water_foam_test.py
                ${PROJECT_SOURCE_DIR}/terrain_unreal/Scripts)
endif()

# The probe of the water of the tiles as Unreal takes it, run by hand (not a test: it reads a
# planet file): per LOD, the falls, the triangles of their curtains and foam, the cells cut, and
# three views of each fall checked against the ground.
add_executable(unreal_water_probe unreal_water_probe.cpp)
target_link_libraries(unreal_water_probe PRIVATE terrain_unreal_bridge terrain_warnings)
add_dependencies(check unreal_water_probe)
