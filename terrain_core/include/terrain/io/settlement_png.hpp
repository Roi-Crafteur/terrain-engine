#pragma once

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/settlement/kingdoms.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/io/png.hpp"

#include <cstdint>
#include <filesystem>

namespace terrain {

// Control images of the analysis maps of the settlement (docs/PEUPLEMENT.md), the whole world in
// one picture, north (row 0) at the top, scale cells per pixel (planetImageScale): the mean of the
// land cells of each pixel over the gray relief, the sea dark.

enum class SettlementPalette : std::uint8_t {
    Fertility,    // arable land, food: brown, yellow, green
    Habitability, // dark purple, red, orange, pale yellow
    Prana,        // gray, light green, deep green
    Mana,         // navy, blue, violet, pale magenta
    Danger,       // pale, orange, dark red
    Resource,     // dark gray to gold
    Roughness,    // pale to dark brown
    Distance,     // km: blue at 0, white from 100
};

[[nodiscard]] Image renderSettlementMap(const PlanetLevel& level,
                                        const settlement::SettlementMaps& maps,
                                        const planet::LandValues<std::uint8_t>& values,
                                        SettlementPalette palette, int scale);
// The nearest terrain model of each cell, one color per model (the color of the pixel's middle
// land cell).
[[nodiscard]] Image renderTerrainModels(const PlanetLevel& level,
                                        const settlement::SettlementMaps& maps,
                                        const settlement::SettlementConfig& config, int scale);
// The Mana with the network over it: the telluric lines (white, the minor ones pale blue), the
// nodes (yellow discs, orange when only partly known), the dead zones (black circles), the
// arcane craters (magenta rings), the volcanoes (red dots, bright when active).
[[nodiscard]] Image renderManaNetwork(const PlanetLevel& level,
                                      const settlement::SettlementMaps& maps, int scale);

// The kingdoms of a preset over the gray relief: one color per state (the major kingdoms
// saturated, the minor states paler), the wild land in its grays; the land borders, by the kind
// most of the edges of the pixel follow: river blue, ridge white, coast cyan, none dark red; the
// capitals in white squares, larger for the major kingdoms (settlement_png_kingdoms.cpp).
[[nodiscard]] Image renderKingdoms(const PlanetLevel& level,
                                   const settlement::ExpansionTerrain& terrain,
                                   const settlement::Kingdoms& kingdoms,
                                   const settlement::BorderRule& rule, int scale);

// Writes settlement_<map>.png for roughness, water_km, terrain_models, arable, food, danger,
// prana, mana, mana_network, habitability, and resource_<id>.png for each resource, into
// directory (created if needed), at most maxWidth pixels wide. Returns false if a file cannot be
// written.
[[nodiscard]] bool exportSettlementMapPngs(const PlanetLevel& level,
                                           const settlement::SettlementMaps& maps,
                                           const settlement::SettlementData& data,
                                           const std::filesystem::path& directory,
                                           int maxWidth = 2048);

} // namespace terrain
