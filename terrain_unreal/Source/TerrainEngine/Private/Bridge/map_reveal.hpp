#pragma once

// Pure C++ layer of the Unreal plugin (no Unreal header): compiled by Unreal with the plugin and
// by CMake for its tests (tests/unreal_map_*_test.cpp).
//
// The parts of the world the player has discovered, for the map and the minimap of the game later
// (agent 35 leaves only the place for it): the dev map shows everything. A game would keep the
// places it has seen (in its save) and the widgets would veil the rest of each tile.

#include "terrain/map/map_frame.hpp"

namespace terrain::unreal {

class MapReveal {
public:
    virtual ~MapReveal() = default;
    // How much of the point (x, z) the map shows: 0 hidden, 1 shown.
    [[nodiscard]] virtual float revealedAt(double x, double z) const = 0;
    // Whether a tile is shown whole (the widgets then draw it without a veil).
    [[nodiscard]] virtual bool wholeTile(const MapTileKey& key) const = 0;
};

// The dev map: everything shown.
class RevealEverything final : public MapReveal {
public:
    [[nodiscard]] float revealedAt(double, double) const override { return 1.0f; }
    [[nodiscard]] bool wholeTile(const MapTileKey&) const override { return true; }
};

} // namespace terrain::unreal
