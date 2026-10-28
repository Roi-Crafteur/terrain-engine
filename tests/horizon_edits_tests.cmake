# Tests of the player edits seen from the far horizon (phases 13 and 15), included by
# CMakeLists.txt after terrain_add_test and the library of the Unreal bridge.
terrain_add_test(horizon_edit_columns_test terrain_core)
terrain_add_test(horizon_edits_test terrain_core)
terrain_add_test(horizon_edit_flow_test terrain_core)
terrain_add_test(horizon_edit_seams_test terrain_core)
terrain_add_test(horizon_edits_cost_test terrain_core)
terrain_add_test(unreal_horizon_edits_test terrain_unreal_bridge)
# A measure of time.
set_tests_properties(horizon_edits_cost_test PROPERTIES RUN_SERIAL TRUE)
