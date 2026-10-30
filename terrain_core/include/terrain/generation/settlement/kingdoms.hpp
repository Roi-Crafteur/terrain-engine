#pragma once

#include "terrain/generation/settlement/borders.hpp"
#include "terrain/generation/settlement/capitals.hpp"
#include "terrain/generation/settlement/expansion.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/generation/settlement/suitability.hpp"
#include "terrain/generation/settlement/world_preset.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace terrain {
class PlanetLevel;
}

// The kingdoms of a world at the start (docs/PEUPLEMENT.md, section 4 quater): the preset drawn,
// the states of each people, their capitals, their expansion, the cleanup of the artifacts, and
// the figures of the tests of a good world. Derived data: the same seed, the same preset and the
// same data give the same kingdoms to the bit, whatever the threads.
namespace terrain::settlement {

// The figures of the tests of a good world.
struct KingdomStats {
    int peoplesWithoutState = 0; // peoples with a share of states but none holding land
    double heldShare = 0.0;      // of the land
    double largestState = 0.0;   // of the land
    double largestPeople = 0.0;  // of the land held
    int largestPeopleIndex = -1;
    int capitalFails = 0; // outside the most habitable fifth, or far from fresh water
    // The hostile land (ExpansionTerrain::hostile): its cells, the share of them held, and held by
    // a people that does not like them.
    std::size_t hostileCells = 0;
    double hostileHeld = 0.0;
    double hostileUnliked = 0.0;
    BorderStats borders;
    MassifStats massifs;
};

struct KingdomTimings {
    double capitals = 0.0;
    double expansion = 0.0;
    double cleanup = 0.0;
    double analysis = 0.0;
};

struct Kingdoms {
    ResolvedPreset preset;
    std::vector<StateCount> counts; // by people
    std::vector<StatePlan> states;  // their targets as last used (after the reruns)
    CapitalChoice capitals;
    std::vector<std::uint16_t> owner;   // by land slot: 0 wild, s + 1 (cleaned)
    std::vector<std::uint16_t> passage; // by strait slot: the state that crossed it first
    std::vector<std::size_t> cells;     // by state, after the cleanup
    int reruns = 0;                     // of the expansion for a people above its share
    std::size_t pushes = 0;
    std::vector<ClimateCut> cuts;
    KingdomStats stats;
    KingdomTimings timings;

    [[nodiscard]] std::size_t memoryBytes() const;
    // FNV-1a 64 of the counts, the targets, the capitals, the owners and the passages.
    [[nodiscard]] std::uint64_t checksum() const;
};

[[nodiscard]] Kingdoms buildKingdoms(const PlanetLevel& level, const SettlementMaps& maps,
                                     const SuitabilityMaps& suitability,
                                     const ExpansionTerrain& terrain, const SettlementData& data,
                                     const SettlementRequest& request, std::uint64_t worldSeed,
                                     int threads);

// The figures of a good world for owners (by land slot) over the terrain, the people of each
// state in states (all but capitalFails, which the capitals give).
[[nodiscard]] KingdomStats kingdomStats(const ExpansionTerrain& terrain,
                                        const SettlementMaps& maps,
                                        const SuitabilityMaps& suitability,
                                        const SettlementData& data,
                                        const std::vector<StatePlan>& states,
                                        const std::vector<std::uint16_t>& owner,
                                        const std::vector<ClimateCut>& cuts);

} // namespace terrain::settlement
