# The load of the world at the spawn and after a teleport (agent 38): the timeline of a load
# (Bridge), and the bench that measures it in this container. Included once, at the end of
# tests/CMakeLists.txt (after the Bridge library).

target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/load_timeline.cpp)

# The timeline of a load: its milestones on a selection made by hand, the curve per band of
# distance and the CSV, the records of several threads, a whole load of the streamer traced.
terrain_add_test(unreal_load_timeline_test terrain_unreal_bridge)
target_include_directories(unreal_load_timeline_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

