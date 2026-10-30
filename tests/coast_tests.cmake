# The coastal relief and the sea cliffs (agent 33). The tests of a task live in a file of their
# own, included once from tests/CMakeLists.txt, so that the list stays under 500 lines.
terrain_add_test(coast_outside_test terrain_core)
terrain_add_test(coast_kinds_test terrain_core)
