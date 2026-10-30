# The tests of the rock of the streamed world (phase 12A): vertical walls, overhangs, niches and
# arches, and the views that show them. Included by tests/CMakeLists.txt.

terrain_add_test(mesh_view_test terrain_core)
terrain_add_test(rock_faces_test terrain_core)
# The rock of the tiles, with their internals (src/streaming/rock_tile.hpp).
terrain_add_test(rock_tiles_test terrain_core)
target_include_directories(rock_tiles_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/streaming)
# The faces of the walls of the mountains, with their internals (src/generation/wall_faces.hpp).
terrain_add_test(wall_faces_test terrain_core)
target_include_directories(wall_faces_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
# The walls of the mountains by the K2 in the streamed tiles.
terrain_add_test(rock_walls_test terrain_core)
# The outer sides of the tiles against the horizon.
terrain_add_test(rock_horizon_test terrain_core)
# The seams between the tiles of the walls (lod_seams.cpp with the rock).
terrain_add_test(rock_seams_test terrain_core)
# The meshes of the tiles of the walls: no opening, no triangle turned against its normals, the
# same normals across the sides of the tiles.
terrain_add_test(rock_mesh_test terrain_core)
# No regular pattern in the rock of the walls: its vertical joints and the light of its faces,
# with the internals of the faces (src/generation/wall_faces.hpp).
terrain_add_test(rock_patterns_test terrain_core)
target_include_directories(rock_patterns_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
# No blade of rock nor slot of air one column wide added by the faces of the walls.
terrain_add_test(rock_thin_test terrain_core)
# The normals of the meshes of the tiles against their triangles (src/streaming/tile_normals.hpp).
terrain_add_test(tile_normals_test terrain_core)
target_include_directories(tile_normals_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/streaming)
# The overhanging folds: lips as slabs, their ceilings unbroken along the folds, with the
# internals of the tiles (src/streaming/rock_tile.hpp) for the synthetic lips.
terrain_add_test(rock_folds_test terrain_core)
target_include_directories(rock_folds_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/streaming)
# The walk to the foot of the K2 on the collision of the tiles: no hole, no step.
terrain_add_test(rock_walk_test terrain_core)
# The slots of the falls: they wind whatever the slope, their banks never step nor rise on a
# levee, no pattern repeats along them.
terrain_add_test(river_slots_test terrain_core)
# The steps of the falls: of all heights and spacings, in cascades too, their lips turned and
# shaped by the hardness of the rock, a bowl at their foot, heaps of blocks below them.
terrain_add_test(river_steps_test terrain_core)
