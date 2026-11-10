# The world the player edits (agent 41): the hollows of the sphere tools and the light of their
# normals, the capsule of the character in the tunnels dug block by block, the edits seen by the
# coarse tiles and from far away, the light floor of the edited surfaces. Included by
# CMakeLists.txt after the library of the Unreal bridge.
terrain_add_test(edit_hollow_test terrain_unreal_bridge)
terrain_add_test(edit_tunnel_test terrain_core)
terrain_add_test(edit_lod_relay_test terrain_unreal_bridge)
terrain_add_test(edit_lod_cost_test terrain_core)
terrain_add_test(edit_lod_views_test terrain_edit_views)
# The edit weights of the vertices and the light floor of the edited surfaces in Unreal.
terrain_add_test(unreal_edit_light_test terrain_unreal_bridge)
find_package(Python COMPONENTS Interpreter QUIET)
if(Python_Interpreter_FOUND)
    add_test(NAME unreal_edit_light_scripts_test
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unreal_edit_light_scripts_test.py
                ${PROJECT_SOURCE_DIR}/terrain_unreal/Scripts
                ${PROJECT_SOURCE_DIR}/terrain_unreal/Source/TerrainEngine/Private)
endif()
