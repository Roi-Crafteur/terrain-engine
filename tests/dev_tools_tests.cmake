# The tools of the developers in the game (agent 36): the places of data/dev/places.json and their
# strict JSON (terrain_core dev/), then the pure C++ of the Unreal tools (Bridge). Included once,
# at the end of tests/CMakeLists.txt (after the Bridge library).

# The strict JSON of the files of the developers: what it reads, refuses, and its numbers.
terrain_add_test(dev_json_test terrain_core)

# The places: the file of the repository (valid and canonical), the exact round trip, the
# refusals, the members of other readers kept, the atomic writes, the patterns of Terrain.Views.
add_executable(dev_places_file_test dev_places_file_test.cpp)
target_link_libraries(dev_places_file_test PRIVATE terrain_core terrain_warnings)
add_test(NAME dev_places_file_test
    COMMAND dev_places_file_test ${PROJECT_SOURCE_DIR}/data/dev/places.json)
add_dependencies(check dev_places_file_test)

# The Bridge of the tools (pure C++, compiled by Unreal with the plugin).
target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/dev_commands.cpp
                                             ${unreal_bridge}/place_pose.cpp)

# The poses of the views (the yaw of a heading, the eye above the ground or the water, a saved
# view posed again) and the arguments of the console commands.
terrain_add_test(unreal_place_pose_test terrain_unreal_bridge)

# What the screen of F3 and Terrain.Info say of a point: the lines of each provider on the small
# world with its water and without it, the surface on or off the heightmap, the ray of the camera
# marched to the ground, the LOD of the tile shown at a chunk.
target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/ground_ray.cpp
                                             ${unreal_bridge}/point_info.cpp)
terrain_add_test(unreal_point_info_test terrain_unreal_bridge)

# The page of the performance (the sliding window, the totals of the timings), the wait for the
# streaming to settle before an image, the names and lines of a run of views, and the colors of
# the view modes with the vertices off the heightmap.
target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/debug_colors.cpp
                                             ${unreal_bridge}/dev_perf.cpp
                                             ${unreal_bridge}/shot_waiter.cpp)
terrain_add_test(unreal_dev_perf_test terrain_unreal_bridge)
terrain_add_test(unreal_debug_colors_test terrain_unreal_bridge)

# The planet file of Terrain.Seed: the file a seed reads and where it may be built, a build on a
# thread of its own, the files of the user never written, a cancelled build.
target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/planet_job.cpp)
terrain_add_test(unreal_planet_job_test terrain_unreal_bridge)
