# The volcanoes (agent 45, volcanoes.hpp): their sites from the geology, their forms, the world
# with and without them. Included once from tests/CMakeLists.txt.
terrain_add_test(volcano_sites_test terrain_core)
target_compile_definitions(volcano_sites_test PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement")
