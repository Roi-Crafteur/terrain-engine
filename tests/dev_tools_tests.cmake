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
