#pragma once

#include <string>
#include <vector>

// The rules of the kingdoms in the configuration of the world (docs/PEUPLEMENT.md, sections 3
// and 4 quater): the world presets, the target area of the states, the choice of the capitals.
// Part of SettlementConfig, read from the same world.json.
namespace terrain::settlement {

// A value of a preset: drawn from the seed in [min, max] (mode "range"), or forced (mode
// "exact", min = max = the value).
struct PresetValue {
    bool exact = false;
    double min = 0.0;
    double max = 0.0;
};

// A world preset (section 3): the share of the land the states hold at the start, the counts of
// the major kingdoms and of the minor states of the reference planet (scaled to the area of the
// world when drawn in a range; an exact count is taken as is).
struct WorldPreset {
    std::string id;
    PresetValue civilizedShare;
    PresetValue majorKingdoms;
    PresetValue minorStates;
};

// The target area of the states: the civilized land shared between the major kingdoms
// (majorAreaShare of it) and the minor states, each its equal part times 1 +- areaSpread (drawn
// from the seed), at most maxLandShare of the land.
struct StateRule {
    double majorAreaShare = 0.7;
    double areaSpread = 0.3;
    double maxLandShare = 0.15;
};

// The capitals (section 4 quater): among the cells in the topHabitability share of the most
// habitable land and at most maxFreshWaterKm from fresh water, the local maxima of the
// suitability of its people within seedRadius cells; at least spacing times the sum of the radii
// of the two states (the radius of a disc of the target area) from the capitals already placed,
// the spacing relaxed by relax up to relaxSteps times when no seed is far enough.
struct CapitalRule {
    double topHabitability = 0.2;
    double maxFreshWaterKm = 10.0;
    int seedRadius = 2;
    double spacing = 1.0;
    double relax = 0.75;
    int relaxSteps = 4;
};

} // namespace terrain::settlement
