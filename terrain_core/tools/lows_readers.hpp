#pragma once

// What the readers of the world make of the dry land below the sea level (terrain_export_lows,
// agent 52): its biome (the ecology with the true water of the column), the surface blend and the
// plants of the tiles of LOD 0 over it, and two counts for the other agents: the low land far from
// the sea that the biomes make beaches or marshes (agent 49), and the river points whose water is
// held at the sea level over ground lower than it in a dry low (Hydrology, agent 31).

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <cstddef>
#include <vector>

namespace lows_tool {

struct Point {
    double x = 0.0;
    double z = 0.0;
};

struct ReaderFigures {
    std::size_t points = 0;     // dry points below 0 m probed
    std::size_t biomeOcean = 0; // their biome (Ecology::sampleAt with their true water)
    std::size_t biomeBeach = 0;
    std::size_t biomeMarsh = 0;
    std::size_t tiles = 0;      // tiles of LOD 0 over them
    std::size_t blendOcean = 0; // the nearest texel of the surface blend: ocean first
    std::size_t blendWater = 0; // its material water first
    std::size_t plantsBelow = 0; // plants of those tiles below 0 m
    std::size_t plantsTotal = 0;
    std::size_t lowPoints = 0;  // dry points from 0 to 15 m, over 40 km from the sea
    std::size_t lowBeach = 0;   // their biome
    std::size_t lowMarsh = 0;
    std::size_t riverPoints = 0; // river points probed near the dry lows
    std::size_t riverHeld = 0;   // at the sea level over ground below -1 m, far from the sea
    std::vector<Point> heldExamples;
};

// The figures of the dry points below 0 m and of the low points far from the sea (at most
// limit of each, spread over the lists), the tiles of those tiles (with the ecology of the
// world), and the river points of the reaches near the centers given.
[[nodiscard]] ReaderFigures measureReaders(const terrain::Heightfield& field,
                                           const terrain::TerrainTiles& tiles,
                                           const std::vector<Point>& dryBelow,
                                           const std::vector<Point>& lowFar,
                                           const std::vector<Point>& lowCenters,
                                           std::size_t limit);

} // namespace lows_tool
