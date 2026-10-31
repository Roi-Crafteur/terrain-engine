#pragma once

#include "terrain/generation/settlement/people.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The world preset of a world and its states (docs/PEUPLEMENT.md, sections 3 and 6): the values
// of the preset drawn from the seed (or forced), the states shared between the peoples by their
// normalized shares and the validated rounding, the target area of each state.
namespace terrain::settlement {

// What the creation of a world asks: a preset (empty: the default of the configuration), and
// values forced over it (mode "exact"), taken as they are.
struct SettlementRequest {
    std::string preset;
    std::optional<double> civilizedShare;
    std::optional<int> majorKingdoms;
    std::optional<int> minorStates;
};

// The values a world uses, kept with it (resolvedMajorKingdoms, resolvedMinorStates).
struct ResolvedPreset {
    std::string preset;
    double civilizedShare = 0.0;
    int majorKingdoms = 0;
    int minorStates = 0;
};

// The values of the request: civilizedShare drawn in its range from the stream "civilizedShare",
// majorKingdoms from "kingdoms" and minorStates from "minorStates" (tirage(seed + "kingdoms",
// min, max)), the counts of a range scaled by scale (the area of the world over the reference
// planet). Throws std::invalid_argument "Invalid preset ..." for an unknown preset or a forced
// value out of bounds.
[[nodiscard]] ResolvedPreset resolvePreset(const SettlementConfig& config,
                                           const SettlementRequest& request,
                                           std::uint64_t worldSeed, double scale);

struct StateCount {
    int major = 0;
    int minor = 0;
};

// The states of each people (section 6, validated): each category on its own, total times the
// normalized share rounded to the nearest; every people with a share above 0 gets at least one
// state in all (a minor state if it has a minor share, else a major kingdom); the difference to
// the total is then corrected one state at a time on the peoples whose exact count was the
// nearest to the rounding threshold, never taking the only state of a people; on a tie the smaller
// id keeps the priority (it gains first and loses last). By peoples.
[[nodiscard]] std::vector<StateCount> allocateStates(const std::vector<People>& peoples,
                                                     int majorKingdoms, int minorStates);

enum class StateRank : std::uint8_t { Major, Minor };

// A state before its expansion: its people, its rank and the land it aims at, in cells.
struct StatePlan {
    int people = 0;
    StateRank rank = StateRank::Major;
    double targetCells = 0.0;
};

// The states of the counts, the major kingdoms first, then the minor states, each people in the
// order of its id. The civilized land (civilizedShare of landCells) goes for majorAreaShare of it
// to the major kingdoms (all of it to one rank when the other has none), in equal parts times
// 1 +- areaSpread drawn from the stream "stateArea", each at most maxLandShare of the land.
[[nodiscard]] std::vector<StatePlan> planStates(const SettlementConfig& config,
                                                const ResolvedPreset& preset,
                                                const std::vector<People>& peoples,
                                                const std::vector<StateCount>& counts,
                                                std::size_t landCells, std::uint64_t worldSeed);

} // namespace terrain::settlement
