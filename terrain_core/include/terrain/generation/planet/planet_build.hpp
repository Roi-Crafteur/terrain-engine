#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_features.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_lakes.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/planet/planet_rivers.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

// Wall-clock seconds of each stage of a build (not part of the result).
struct PlanetTimings {
    double relief = 0.0;
    double ocean = 0.0;
    double climate = 0.0;
    double order = 0.0;
    double hierarchy = 0.0;
    double lakes = 0.0;
    double flow = 0.0;
    double rivers = 0.0;
    double features = 0.0;
    double breach = 0.0;
    double passes = 0.0; // the cols between the cells

    [[nodiscard]] double total() const {
        return relief + ocean + breach + climate + order + hierarchy + lakes + flow + rivers +
               features + passes;
    }
};

// Water balance of the land, in units of 1e-6 m^3/s (planet_climate): all the runoff reaches
// the sea or evaporates from the lakes, to the unit.
struct PlanetWaterBudget {
    std::int64_t runoff = 0;
    std::int64_t toOcean = 0;
    std::int64_t lakeEvaporation = 0;
};

// The planet level in full precision, as computed: what the tests check and what PlanetLevel
// compacts.
struct PlanetBuild {
    PlanetGrid grid;
    std::vector<float> height;            // meters: the floors, breached (PlanetParams)
    std::vector<std::uint8_t> ocean;      // OceanKind
    // Per cell: its floor point (planet::floorPoint, kCenterFloor for the center), and 1 where
    // the mountain mask is above 0 there.
    std::vector<std::uint8_t> floor;
    std::vector<std::uint8_t> mountain;
    PlanetBreach breach;
    PlanetPasses passes;
    planet::PlanetClimate climate;        // precipitation, row temperatures
    std::vector<std::uint8_t> receiver;   // direction, kNoNeighbor
    std::vector<std::int32_t> lakeOfCell; // lake covering each cell, or -1
    std::vector<PlanetLake> lakes;
    std::vector<float> discharge; // m^3/s leaving each dry land cell
    RiverGraph rivers;
    std::vector<RiverFeature> features;
    PlanetWaterBudget budget;
    PlanetTimings timings;
};

namespace planet {

// Every stage on the grid of the world of the field: relief, ocean, climate, total order,
// depression hierarchy, lakes, discharge, river graph and features. The same field and
// parameters give the same bits whatever params.threads. Each stage frees what the next ones
// do not need. Throws std::invalid_argument for a cell size that does not fit the world.
[[nodiscard]] PlanetBuild buildPlanet(const Heightfield& field, const PlanetParams& params);

} // namespace planet

} // namespace terrain
