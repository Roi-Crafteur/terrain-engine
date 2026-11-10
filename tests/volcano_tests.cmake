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
# The goldens with the volcanoes on (golden_support.hpp: the same executables run with --flags
# volcanoes), their values pinned for R1, which turns every new relief on together. Not in ctest:
# the planet level of the default seed with them on, e1db9106a2b0b923 (terrain_make_planet, about
# 90 s on 4 cores), and planet_file_test, whose bytes carry the versions R1 raises.
foreach(golden heightfield_golden_test planet_golden_test planet_memory_test river_falls_test
        hydrology_test water_level_test coast_outside_test mountain_freeze_test
        unreal_determinism_check_test)
    add_test(NAME ${golden}_volcanoes COMMAND ${golden} --flags volcanoes)
endforeach()
