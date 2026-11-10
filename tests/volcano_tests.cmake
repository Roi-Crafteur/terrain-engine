# The volcanoes (agent 45, volcanoes.hpp): their sites from the geology, their forms, the world
# with and without them. Included once from tests/CMakeLists.txt.
terrain_add_test(volcano_sites_test terrain_core)
target_compile_definitions(volcano_sites_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement")
terrain_add_test(volcano_forms_test terrain_core)
terrain_add_test(volcano_outside_test terrain_core)
terrain_add_test(map_volcanoes_test terrain_core)
terrain_add_test(volcano_calderas_test terrain_core)
terrain_add_test(volcano_cost_test terrain_core)
# A measure of time: alone, even under ctest -j.
set_tests_properties(volcano_cost_test PROPERTIES RUN_SERIAL TRUE)
terrain_add_test(volcano_water_test terrain_core)
terrain_add_test(volcano_surface_test terrain_core)
terrain_add_test(volcano_horizon_test terrain_core)
# Where the volcanoes stand: 100 positions pinned before the draw moved into generation/, and the
# settlement reading the same list whatever its data.
terrain_add_test(volcano_positions_test terrain_core)
target_include_directories(volcano_positions_test PRIVATE
    ${PROJECT_SOURCE_DIR}/terrain_core/src/generation)
target_compile_definitions(volcano_positions_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement")
