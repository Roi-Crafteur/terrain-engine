# The load of the world at the spawn and after a teleport (agent 38): the timeline of a load
# (Bridge), and the bench that measures it in this container. Included once, at the end of
# tests/CMakeLists.txt (after the Bridge library).

target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/load_phase.cpp
                                             ${unreal_bridge}/load_timeline.cpp)

# The rhythm of the loads (core): the order of the requests and integrations made by hand, the
# budget and the limit of the workers changed during a load, the same tiles bit for bit.
terrain_add_test(stream_loading_test terrain_core)

# The far horizon brought to a new place level by level, finest first (core, planFinest).
terrain_add_test(horizon_progressive_test terrain_core)

# The GPU horizon brought to a new place level by level, finest first (Bridge): one more level
# at each commit, a jump in the middle of an epoch, more or fewer pieces at once, the same view.
terrain_add_test(unreal_horizon_progressive_test terrain_unreal_bridge)
target_include_directories(unreal_horizon_progressive_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The timeline of a load: its milestones on a selection made by hand, the curve per band of
# distance and the CSV, the records of several threads, a whole load of the streamer traced.
terrain_add_test(unreal_load_timeline_test terrain_unreal_bridge)
target_include_directories(unreal_load_timeline_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The phase of a load (Bridge): the budget within the cap, the fade and its cap, the pawn held
# until its collision, the prepared point with the streamer, the direction of a heading and pitch.
terrain_add_test(unreal_load_phase_test terrain_unreal_bridge)
target_include_directories(unreal_load_phase_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The load of the tiles simulated from their costs (tests/load_schedule.cpp), to compare the orders
# of the loads: tested on costs made by hand, used by the bench.
add_library(terrain_load_schedule STATIC load_schedule.cpp)
target_include_directories(terrain_load_schedule PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(terrain_load_schedule PUBLIC terrain_core PRIVATE terrain_warnings)
terrain_add_test(load_schedule_test terrain_load_schedule)

# The bench of the loads, run by hand (not a test: tens of seconds, a planet file optionally):
# unreal_loading_bench [--planet <file>] [--at <x> <z>] [--teleport <x> <z>]... (see the file).
add_executable(unreal_loading_bench unreal_loading_bench.cpp)
target_link_libraries(unreal_loading_bench PRIVATE terrain_unreal_bridge terrain_load_schedule
                                                   terrain_warnings)
add_dependencies(check unreal_loading_bench)
