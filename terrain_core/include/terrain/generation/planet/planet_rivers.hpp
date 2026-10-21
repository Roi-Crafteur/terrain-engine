#pragma once

#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_flow.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_lakes.hpp"
#include "terrain/generation/planet/planet_params.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

// How a river segment ends.
enum class RiverMouth : std::uint8_t {
    Confluence,   // it joins another segment (downstream)
    Sea,          // it reaches the sea
    Lake,         // it flows into a lake that spills on (the river goes on from its outlet)
    TerminalLake, // it flows into a lake without outlet, where its water evaporates
    ClosedBasin,  // it ends in a dry pit (a playa)
    Ice,          // it reaches frozen ground: no liquid river beyond
};

// A point of a river: a cell center, in meters of the world. The points of a segment follow
// each other across the seams (each is the image of its cell nearest to the previous one), so
// a polyline never jumps by a period; the first point is in the canonical range.
struct RiverVertex {
    double x = 0.0;
    double z = 0.0;
    float elevation = 0.0f; // the altitude of the cell, or the water surface at a sea or lake mouth
    float discharge = 0.0f; // m^3/s
    float width = 0.0f;     // meters
    float depth = 0.0f;     // meters
    std::uint32_t cell = 0;
};

// A segment of the river graph: from a source, a confluence or a lake outlet, down to the next
// confluence (its last point is the first of the segment it joins) or to its mouth (its last
// point is the cell of the sea, the lake or the ice it reaches). No confluence inside.
struct RiverSegment {
    std::vector<RiverVertex> vertices;
    std::uint8_t strahler = 1;
    RiverMouth mouth = RiverMouth::Confluence;
    std::int32_t downstream = -1;          // segment joined at a confluence
    std::int32_t mouthLake = -1;           // lake of a Lake, TerminalLake or ClosedBasin mouth
    std::vector<std::int32_t> upstream;    // segments ending at its first point, by index
    std::vector<std::int32_t> sourceLakes; // lakes whose outflow starts at its first point
    double length = 0.0;                   // meters, along the polyline
};

struct RiverGraph {
    std::vector<RiverSegment> segments;
    std::vector<std::uint8_t> lakeStrahler; // per lake: Strahler order of its outflow
};

namespace planet {

// The river graph: the river cells (dry land, not frozen, at least minDischarge, and not a dry
// pit that no river feeds) linked by their receivers. Strahler orders in one pass in decreasing
// total order (the inputs of a cell come before it; a lake is a confluence of the rivers entering
// it, and its outflow starts at its partner when the pass reaches its pass). Segments start at
// every river cell without exactly one river cell above it (but a dry pit, where the rivers
// reaching it end), sorted by cell index.
[[nodiscard]] RiverGraph buildRiverGraph(const PlanetGrid& grid, const std::vector<float>& height,
                                         const std::vector<std::uint8_t>& ocean,
                                         const std::vector<std::uint8_t>& receiver,
                                         const std::vector<std::uint32_t>& order,
                                         const LakeSolution& lakes, const PlanetFlow& flow,
                                         const WaterOf& water, const PlanetRiverParams& params);

// Bankfull width and depth of a river of discharge Q (m^3/s).
[[nodiscard]] float riverWidth(double discharge, const PlanetRiverParams& params);
[[nodiscard]] float riverDepth(double discharge, const PlanetRiverParams& params);

} // namespace planet

} // namespace terrain
