# The water and the far horizon as Unreal draws them (agent 40). Included once, at the end of
# tests/CMakeLists.txt (after the Bridge library and the fixture of the planet of the coasts).

target_sources(terrain_unreal_bridge PRIVATE ${unreal_bridge}/horizon_sea_line.cpp
                                             ${unreal_bridge}/horizon_split.cpp)

# The line of the sea of the GPU horizon along the coasts: the sea painted up the ground the grid
# shows, and the distance from its coast to the true shore, on the levels 0 to 5 of four places of
# the coasts with the planet file.
terrain_add_test(horizon_sea_line_test terrain_unreal_bridge)
target_include_directories(horizon_sea_line_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(horizon_sea_line_test PROPERTIES FIXTURES_REQUIRED coast_planet
                     ENVIRONMENT TERRAIN_COAST_PLANET=${coast_planet_file})

# The cells of the GPU horizon cut into triangles as its grid cuts them, and as the candidate of
# agent 40 would (horizon_split.hpp): the comb of light of the cliff of the view 3, the contour of
# the walls of three coasts, the teeth of the sky line of falaise_cotiere.
terrain_add_test(horizon_split_test terrain_unreal_bridge)
target_include_directories(horizon_split_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(horizon_split_test PROPERTIES FIXTURES_REQUIRED coast_planet
                     ENVIRONMENT TERRAIN_COAST_PLANET=${coast_planet_file})
