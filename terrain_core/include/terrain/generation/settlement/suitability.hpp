#pragma once

#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/settlement/people.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {
class PlanetLevel;
}

// raceSuitability (docs/PEUPLEMENT.md, section 6): how much a people likes a land cell, in [0, 1].
// Its habitability, its physical score for the preferences of the people, the Prana and the Mana
// it likes (or dislikes), less the danger it is sensitive to, plus a smooth noise of the seed of
// its own, over the theoretical maximum of the people (the sum of its positive weights, as the
// habitability), clamped to [0, 1]; 0 beyond a hard limit of its preferences. No Aura map in V1:
// the Aura preference has no effect.
namespace terrain::settlement {

// The terms of a cell for a people.
struct SuitabilityTerms {
    double habitability = 0.0; // [0, 1]
    double physical = 0.0;     // its physical score, [0, 1]; 0 beyond a hard limit
    double prana = 0.0;        // [0, 1]
    double mana = 0.0;         // [0, 1]
    double danger = 0.0;       // natural, [0, 1]
    double noise = 0.0;        // [-1, 1]
};

// The theoretical maximum of the score of a people.
[[nodiscard]] double suitabilityMax(const People& p, const SuitabilityRule& rule);
[[nodiscard]] double suitabilityOf(const People& p, const SuitabilityRule& rule,
                                   const SuitabilityTerms& t);

// One byte per land cell and per people.
struct SuitabilityMaps {
    std::vector<planet::LandValues<std::uint8_t>> byPeople; // by SettlementData::peoples
    double seconds = 0.0;

    [[nodiscard]] std::size_t memoryBytes() const;
    // FNV-1a 64 of every map, people by people: the same bits whatever the threads.
    [[nodiscard]] std::uint64_t checksum() const;
};

// The raceSuitability of every people of data on the land of maps. The same bits whatever threads.
[[nodiscard]] SuitabilityMaps computeSuitability(const PlanetLevel& level,
                                                 const SettlementMaps& maps,
                                                 const SettlementData& data,
                                                 std::uint64_t worldSeed, int threads);

} // namespace terrain::settlement
