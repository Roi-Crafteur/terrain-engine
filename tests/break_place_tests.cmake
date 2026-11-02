# Breaking and placing in game (agent 28, phase 15): the aim, the face, the tools and the ghost
# (Bridge/block_targeting), the rules of a click and the capsule (Bridge/build_rules), the save of
# the edits of a seed (Bridge/edit_save) and the time from the click to the tile shown
# (Bridge/edit_latency). Included by CMakeLists.txt after the library of the Unreal bridge.
target_sources(terrain_unreal_bridge PRIVATE
    ${unreal_bridge}/block_targeting.cpp
    ${unreal_bridge}/build_rules.cpp
    ${unreal_bridge}/edit_save.cpp
)
terrain_add_test(unreal_block_targeting_test terrain_unreal_bridge)
terrain_add_test(unreal_build_rules_test terrain_unreal_bridge)
terrain_add_test(unreal_edit_save_test terrain_unreal_bridge)
