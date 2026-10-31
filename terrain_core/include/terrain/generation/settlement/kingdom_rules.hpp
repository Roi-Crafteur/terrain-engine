#pragma once

#include "terrain/generation/settlement/preference.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The rules of the kingdoms in the configuration of the world (docs/PEUPLEMENT.md, sections 3
// and 4 quater): the world presets, the target area of the states, the choice of the capitals, the
// costs of the expansion, the borders.
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

// A penalty of the cost of a cell: cost times a ramp of one of its values.
struct CostPenalty {
    Ramp ramp;
    double cost = 0.0;
};

enum class SeaCrossing : std::uint8_t { Straits, Forbidden };

// The slow noise of the seed that makes the cost of the expansion irregular, so that the fronts
// are no circles and the meeting lines no straight lines in open land: the base cost of a cell
// times 1 + amplitude n, n in [-1, 1] (an fbm of octaves octaves from scaleKm), at most 0.9 so
// that no cell is ever free.
struct CostNoise {
    double scaleKm = 50.0;
    int octaves = 2;
    double amplitude = 0.5;
};

// The growth of the minor states along the valleys, in tree-like shapes rather than spots: the
// cost of a cell times 1 - valleyBonus on the floor of a valley (at its drainage: the rivers of
// riverClass or more, so the main valleys, not every stream), rising linearly with its height
// above the drainage to 1 + slopePenalty at slopeHeightM and above (the slopes, the divides). The
// crossing of the rivers does not change.
struct ValleyGrowth {
    int riverClass = 2;
    double valleyBonus = 0.7;
    double slopePenalty = 4.0;
    double slopeHeightM = 40.0;
};

// The expansion (section 6 bis): the cost of entering a cell for a state is the cost of its
// nearest terrain model plus the penalties of its altitude, roughness, aridity (humidity) and
// danger, times the noise of costNoise (and for a minor state the factor of valleys), lowered by
// the suitability of the people (times preferenceWeight, at most 0.6: a liked terrain is cheaper,
// never free), at least minCost.
// Crossing a river costs the crossing of its class (river, large, major, impassable) less the
// probable ford (classes 1 and 2, flat ground: fordBonus of it at roughness 0, nothing from
// fordMaxRoughness) or less confluenceBonus at a confluence (never below 0), half on entering the
// river from a bank and half on leaving it to a bank; following a river costs no crossing. A lake
// costs lakeCost, a ridge ridgeCost more. The sea: with "straits", a sea cell at most straitCells
// from the land costs seaCost and is only crossed, never held; with "forbidden", never entered. A
// state takes no cell where its people's suitability is below minSuitability, nor farther than
// maxCost. From barrierStop times its target it crosses no barrier any more: it may still take
// the rivers and the lakes along its land, but neither follow them away nor leave a river of
// barrierRiverClass or more or a lake for the land beyond, nor cross a ridge, nor enter a hostile
// cell of one of barrierModels (a glacier, a summit) from the land that is not hostile, or the sea:
// it may climb the valleys of a massif up to its crest, never over it; a front closed by barriers
// stops there, an open
// one goes on to the target and then up to the next barrier, at most overflow times the target.
// A people holding more than maxPeopleShare of the land held has the targets of its states
// lowered and the expansion run again, at most peopleReruns times.
struct ExpansionRule {
    double preferenceWeight = 0.5;
    double minCost = 0.1;
    CostPenalty altitude;
    CostPenalty roughness;
    CostPenalty aridity;
    double danger = 4.0;
    CostNoise costNoise;
    ValleyGrowth valleys;
    std::array<double, 4> riverCrossing{3.0, 6.5, 11.5, 25.0}; // classes 1 to 4
    int barrierRiverClass = 1;
    std::vector<std::string> barrierModels; // ids of terrain models
    double fordBonus = 0.6;
    double fordMaxRoughness = 0.25;
    double confluenceBonus = 1.5;
    double lakeCost = 15.0;
    double ridgeCost = 4.0;
    SeaCrossing sea = SeaCrossing::Straits;
    double seaCost = 50.0;
    int straitCells = 2;
    double minSuitability = 0.25;
    double maxCost = 3000.0;
    double barrierStop = 0.7;
    double overflow = 1.3;
    double maxPeopleShare = 0.4;
    int peopleReruns = 3;
};

// The borders: a ridge is an edge between two dry cells of different catchments (each cell drains
// to the first river segment, lake or sea down its receivers) at least ridgeHeightM above the
// lowest land within ridgeWindow cells, where the relief of the geography is at least
// ridgeRelief, and with the land falling on both sides of it (a crest line, never a slope: the
// cell before the edge lower than the first cell, the cell after it lower than the second); a
// border follows the coast when one of its cells is at most coastKm from the sea
// and the two are at different distances from it. The cleanup gives the enclaves of at most
// holeCells cells to the one owner around them. The cuts of the climate are the limits between
// two rows where the mean difference of the rainfall of the land is above cutFactor times the
// median of all the limits and at least cutMinMm.
struct BorderRule {
    double ridgeHeightM = 300.0;
    int ridgeWindow = 2;
    double ridgeRelief = 0.12;
    double coastKm = 16.0;
    int holeCells = 4;
    double cutFactor = 6.0;
    double cutMinMm = 100.0;
};

// What the tests of a good world call the hostile land (section 4 quater): the cells of
// hostileModels or at least highMountainM high; a people likes a cell from likedSuitability.
struct GoodWorldRule {
    std::vector<std::string> hostileModels; // ids of terrain models
    double highMountainM = 3500.0;
    double likedSuitability = 0.5;
};

} // namespace terrain::settlement
