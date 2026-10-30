#pragma once

// The kingdoms of a preset in terrain_export_settlement: the values of the preset, the states of
// each people, their capitals, their expansion and the figures of the tests of a good world;
// kingdoms_<preset>.png (and kingdoms_<preset>_zoom.png of a window) and capitals_<preset>.txt.

#include "settlement_report.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/settlement/expansion.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/generation/settlement/suitability.hpp"
#include "terrain/io/settlement_png.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace settlement_tool {

struct PresetRun {
    std::string preset;
    std::uint64_t seed = 0;
    int threads = 0;
    std::filesystem::path directory;
    int width = 2048;
    const terrain::PlanetWindow* zoom = nullptr; // the window of the zoom, if any
};

// Builds the kingdoms of one preset, reports them and writes their files. Returns false if a file
// cannot be written.
bool runPreset(const terrain::PlanetLevel& level, const terrain::settlement::SettlementMaps& maps,
               const terrain::settlement::SuitabilityMaps& suitability,
               const terrain::settlement::ExpansionTerrain& terrain,
               const terrain::settlement::SettlementData& data, const PresetRun& run, Report& r);

} // namespace settlement_tool
