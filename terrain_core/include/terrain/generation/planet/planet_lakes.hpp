#pragma once

#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_hierarchy.hpp"

#include <cstdint>
#include <vector>

namespace terrain::planet {

// Water state of a depression in the steady state: dry (no water reaches its own level),
// partial (a lake whose evaporation balances its inflow, below its pass) or full (it spills).
enum class DepressionState : std::uint8_t { Dry, Partial, Full };

// A lake of the planet level: the topmost wet node of a branch of the hierarchy. Water in units
// of 1e-6 m^3/s (planet_climate).
struct PlanetLake {
    std::int32_t node = -1;
    bool overflowing = false;        // full: the outflow leaves at the partner of its pass
    bool playa = false;              // its water evaporates before covering one cell
    bool frozen = false;             // most of its cells are frozen
    float level = 0.0f;              // altitude of its surface, meters
    std::uint32_t pit = kNoCell;     // its lowest cell (a playa: where its water vanishes)
    std::uint32_t pass = kNoCell;    // lowest point of its rim
    std::uint32_t partner = kNoCell; // where its outflow goes on
    std::uint32_t cells = 0;         // cells under its water
    double volume = 0.0;             // cubic meters
    std::int64_t inflow = 0;
    std::int64_t evaporation = 0;
    std::int64_t outflow = 0;
};

struct LakeSolution {
    std::vector<DepressionState> state;   // per node (the ocean: Full)
    std::vector<std::int64_t> inflow;     // per node: water entering its subtree
    std::vector<std::int64_t> capacity;   // per node: evaporation of its subtree when full
    std::vector<std::int32_t> lakeOfNode; // per node: index of the lake holding it, or -1
    std::vector<PlanetLake> lakes;
    std::vector<std::int32_t> lakeOfCell; // per cell: the lake covering it, or -1
    std::int64_t runoff = 0;              // total runoff of the land
    std::int64_t toOcean = 0;             // water reaching the ocean
};

// Fill-Spill-Merge in the steady state with evaporation (after Barnes, Callaghan and Wickert
// 2021). Each leaf receives the runoff of its basin (label). A subtree spills if and only if the
// water entering it exceeds its capacity, the extra evaporation of all its cells when full: a
// node is dry, partial or full as its inflow is at most the capacity of its children, at most
// its own, or above. Top-down, tree by tree in decreasing order of the pass where they reach the
// ocean (a transfer always goes to a lower pass), with a Fenwick tree over the leaves: when only
// one child is full by itself its excess goes to the geolink of the other, and a full root sends
// its excess to its geolink, the ocean or a leaf of an earlier tree. The cells of a full node
// are all under water; those of a partial node, lowest first, as long as its water covers their
// evaporation (its level is the altitude of the first one left dry). Exact integer water.
[[nodiscard]] LakeSolution solveLakes(const PlanetGrid& grid, const std::vector<float>& height,
                                      const DepressionHierarchy& hierarchy,
                                      const std::vector<std::uint32_t>& order,
                                      const WaterOf& water);

} // namespace terrain::planet
