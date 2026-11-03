# The coastal relief and the sea cliffs (agent 33). The tests of a task live in a file of their
# own, included once from tests/CMakeLists.txt, so that the list stays under 500 lines.
terrain_add_test(coast_outside_test terrain_core)
terrain_add_test(coast_kinds_test terrain_core)
terrain_add_test(coast_mouths_test terrain_core)
terrain_add_test(coast_patterns_test terrain_core)
target_include_directories(coast_patterns_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
terrain_add_test(coast_sea_test terrain_core)
terrain_add_test(coast_harbors_test terrain_core)
terrain_add_test(coast_reference_test terrain_core)
terrain_add_test(coast_inlet_shapes_test terrain_core)
terrain_add_test(coast_anchorages_test terrain_core)
terrain_add_test(coast_materials_test terrain_core)
# The faces of the coasts in three dimensions: in the tiles and on the horizon, their notches, the
# stacks and the reefs seen from afar.
terrain_add_test(coast_rock_test terrain_core)
# The faces as the materials light them beyond the tiles of LOD 0: the normals of the horizon and
# of the tiles of LOD 1 to 3 along their lines, their rock, the sea at their foot.
terrain_add_test(coast_render_test terrain_core)
# The walls of the coasts as the tiles draw them, seen from afar and from near: lines that move in
# and out, faces that turn, tops that rise and dip, nothing at a regular step.
terrain_add_test(coast_wall_test terrain_core)
# The stacks as the tiles draw them: rock that narrows, irregular, in beds, notched, on an apron,
# of several kinds (a needle, a pillar, a stack split in two, an arch).
terrain_add_test(coast_stack_test terrain_core)
# The coasts seen from afar: the wall of the view 3 at 800 m (no row on its ramp, its bands not
# alike), the sea painted up the left wall of the view fjord_axe.
terrain_add_test(coast_far_test terrain_core)
# The pawn of the game by the faces of the coasts, on the collision of the tiles (the Bridge of the
# tools: the ground of a teleport, the landing of Terrain.Pawn and of a click on the map).
terrain_add_test(coast_pawn_test terrain_unreal_bridge)
# The materials of the coasts as the GPU horizon of Unreal draws them (Bridge).
terrain_add_test(coast_horizon_rock_test terrain_unreal_bridge)
# The rock under the sea in the scripts of the materials of Unreal, without Unreal.
find_package(Python COMPONENTS Interpreter QUIET)
if(Python_Interpreter_FOUND)
    add_test(NAME coast_rock_scripts_test
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/coast_rock_scripts_test.py
                ${PROJECT_SOURCE_DIR}/terrain_unreal/Scripts
                ${PROJECT_SOURCE_DIR}/terrain_unreal/Source/TerrainEngine/Private/Bridge/horizon_textures.hpp)
endif()

# The default world with its planet file, as Unreal loads it: built once (about two minutes on four
# cores, 247 MB) or read when the file already holds the planet of this generator, for the tests
# of the places of the coasts (coast_planet_support.hpp: TERRAIN_COAST_PLANET).
set(coast_planet_file ${CMAKE_BINARY_DIR}/coast_planet.bin)
add_test(NAME coast_planet_file
    COMMAND terrain_probe --planet ${coast_planet_file} --json 0 0)
set_tests_properties(coast_planet_file PROPERTIES FIXTURES_SETUP coast_planet TIMEOUT 1800)
set_tests_properties(coast_rock_test coast_render_test coast_wall_test coast_stack_test
                     coast_far_test coast_pawn_test
                     coast_inlet_shapes_test coast_materials_test coast_patterns_test
                     coast_horizon_rock_test PROPERTIES
    FIXTURES_REQUIRED coast_planet ENVIRONMENT TERRAIN_COAST_PLANET=${coast_planet_file})
