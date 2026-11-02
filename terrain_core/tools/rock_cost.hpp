#pragma once

// The cost of the rock in three dimensions in the tiles (phase 12A), for terrain_export_rock
// --cost: the tiles a client streams around a place, generated with the rock and without it.

#include "terrain/streaming/terrain_tiles.hpp"

#include <string>

namespace rock_tool {

// A place where the cost is measured, and what it stands for.
struct CostPlace {
    std::string name;
    double x = 0.0;
    double z = 0.0;
};

// For each LOD, the tiles around the place (to viewDistance chunks) generated on one thread with
// the rock (rock) and without it (relief), the best of rounds alternate runs: milliseconds per
// tile, the ratio, the share of tiles holding faces and their triangles. One line per LOD.
[[nodiscard]] std::string costAt(const terrain::TerrainTiles& rock,
                                 const terrain::TerrainTiles& relief, const CostPlace& place,
                                 int viewDistance, int rounds);

// How much the view from above of a square of side meters around the place changes when its tiles
// pass from one LOD to the next (all of them at LOD L, then L + 1): the mean difference of the
// pixels, in levels of 255, with the rock and without it. One line per passage. The views at
// each LOD are written to directory as lod<L>_<place>.png (with the rock).
[[nodiscard]] std::string lodJumps(const terrain::TerrainTiles& rock,
                                   const terrain::TerrainTiles& relief, const CostPlace& place,
                                   double side, const std::string& directory, int threads);

} // namespace rock_tool
