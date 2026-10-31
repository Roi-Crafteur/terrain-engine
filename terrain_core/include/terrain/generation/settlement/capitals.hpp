#pragma once

#include "terrain/generation/settlement/expansion.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/generation/settlement/suitability.hpp"
#include "terrain/generation/settlement/world_preset.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// The capitals of the states (docs/PEUPLEMENT.md, section 4 quater). The candidates are the land
// cells in the most habitable share of the land (CapitalRule::topHabitability, a threshold on the
// bytes of the habitability map: the cells at or above it are at most that share) and near fresh
// water; the seeds of a people are the candidates where its suitability is a local maximum among
// the candidates around (ties to the lower cell), best first. The states are served in turn, the
// major kingdoms first, then the minor states, the peoples in the order of their ids: each takes
// the best seed of its people far enough from the capitals already placed, the spacing relaxed
// step by step when none is; then any candidate, then any land cell its people can live on (a
// capital outside the candidates fails the test of a good world). Given compartments, a minor
// state first tries the seeds of its people in a compartment not yet holding a capital whose land
// is between low and high times its target, the size it can fill whole: a valley, an island, a
// bank between two rivers.
namespace terrain::settlement {

struct Capital {
    std::size_t cell = 0;
    double x = 0.0; // the center of the cell, in the coordinates of the engine
    double z = 0.0;
    bool topHabitability = false; // in the most habitable share
    bool nearFreshWater = false;  // at most CapitalRule::maxFreshWaterKm
    bool seed = false;            // a seed of its people (not a fallback)
    bool compartment = false;     // in a compartment of the size of its state
    int relaxations = 0;          // how many times the spacing was relaxed for it
};

struct CapitalChoice {
    std::vector<Capital> capitals; // by state
    std::uint8_t habitabilityThreshold = 0;
    std::size_t candidates = 0;
    std::size_t seeds = 0; // of all the peoples with a state
    double seconds = 0.0;
};

// The radius of a disc of that many cells, in meters.
[[nodiscard]] double stateRadius(double cells, double cellSize);

// The compartments a minor state prefers, by their land against its target.
struct CompartmentFit {
    const Compartments* compartments = nullptr; // none: no preference
    double low = 0.7;
    double high = 1.3;
};

// The capital of every state. The same result whatever threads.
[[nodiscard]] CapitalChoice placeCapitals(const SettlementMaps& maps,
                                          const SuitabilityMaps& suitability,
                                          const CapitalRule& rule,
                                          const std::vector<StatePlan>& states, int threads,
                                          const CompartmentFit& fit = {});

} // namespace terrain::settlement
