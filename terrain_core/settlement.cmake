# The settlement V1 (agent 25, docs/PEUPLEMENT.md): its sources in terrain_core, kept in a file of
# their own, included once from terrain_core/CMakeLists.txt, so that the list of the library
# stays short and the settlement only touches one line of it. Data only: nothing graphic.
target_sources(terrain_core PRIVATE
    src/json/json_parse.cpp
    src/json/json_write.cpp
    src/generation/settlement/analysis_cell.cpp
    src/generation/settlement/analysis_distance.cpp
    src/generation/settlement/borders.cpp
    src/generation/settlement/capitals.cpp
    src/generation/settlement/compartments.cpp
    src/generation/settlement/data_fields.cpp
    src/generation/settlement/expansion.cpp
    src/generation/settlement/expansion_terrain.cpp
    src/generation/settlement/geology.cpp
    src/generation/settlement/geology_plates.cpp
    src/generation/settlement/geology_points.cpp
    src/generation/settlement/kingdoms.cpp
    src/generation/settlement/map_influences.cpp
    src/generation/settlement/map_inputs.cpp
    src/generation/settlement/people.cpp
    src/generation/settlement/preference.cpp
    src/generation/settlement/resource_def.cpp
    src/generation/settlement/settlement_config.cpp
    src/generation/settlement/settlement_config_parts.cpp
    src/generation/settlement/settlement_config_kingdoms.cpp
    src/generation/settlement/settlement_data.cpp
    src/generation/settlement/settlement_maps.cpp
    src/generation/settlement/suitability.cpp
    src/generation/settlement/telluric.cpp
    src/generation/settlement/world_preset.cpp
)
# The PNG control images of the maps (io/: no Unreal wrapper).
target_sources(terrain_core PRIVATE src/io/settlement_png.cpp src/io/settlement_png_kingdoms.cpp)

# terrain_export_settlement <output-dir> [--planet <file>] [--rebuild-planet] [--seed <seed>]
#     [--world <x> <z>] [--cell <meters>] [--threads <n>] [--data <dir>] [--width <pixels>]
#     [--equity-seeds <n>] [--preset <id>|all]
add_executable(terrain_export_settlement tools/export_settlement.cpp tools/settlement_kingdoms.cpp
                                         tools/settlement_report.cpp
                                         tools/process_memory.cpp)
target_link_libraries(terrain_export_settlement PRIVATE terrain_core terrain_warnings)
target_compile_definitions(terrain_export_settlement PRIVATE
    TERRAIN_SETTLEMENT_DATA="${PROJECT_SOURCE_DIR}/data/settlement")
