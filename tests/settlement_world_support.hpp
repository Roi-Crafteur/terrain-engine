#pragma once

// The world of the tests of the settlement: the default world (its real continents, mountains,
// deserts and ice) with cells of 65,536 m and one relief sample per cell, built in a few seconds;
// the data files of the repository.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace settlement_test {

inline constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
inline constexpr double kCell = 65536.0;

inline terrain::PlanetParams coarseParams(int threads) {
    terrain::PlanetParams p;
    p.cellSize = kCell;
    p.floorSamples = 1;
    p.threads = threads;
    return p;
}

struct World {
    terrain::WorldSize world;
    terrain::Heightfield field{kSeed, terrain::HeightfieldParams{}, world};
    terrain::PlanetLevel level{kSeed, terrain::HeightfieldParams{}, coarseParams(0), world};
    terrain::settlement::SettlementData data =
        terrain::settlement::loadSettlementData(TERRAIN_SETTLEMENT_DATA);
};

inline double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace settlement_test
