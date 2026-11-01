#pragma once

// Internal to the settlement: the scores of one land cell from what is read around it, pure
// functions of the configuration (docs/PEUPLEMENT.md, sections 4, 4 ter, 6 bis).

#include "terrain/generation/settlement/preference.hpp"
#include "terrain/generation/settlement/resource_def.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"

#include <cstdint>

namespace terrain::settlement {

struct CellInputs {
    // The criteria; their roughness is filled by scoreCell.
    CriterionValues criteria{};
    double sampleRange = 0.0; // meters between the lowest and highest of 4 heights in the cell
    double floorRange = 0.0;  // meters between the lowest and highest floors of the 3 x 3 cells
    double relief = 0.0;     // intensity of the relief of the geography
    double belt = 0.0;       // the belts of the collisions
    double character = 0.5;  // worn (0) to sharp (1)
    std::uint8_t water = 0;  // the bits of SettlementMaps::water
    int riverNear = 0;       // the largest class of river within one cell
    bool saltNear = false;   // a salt lake within one cell
    double lines = 0.0;      // the influences of the magic and of the geology
    double nodes = 0.0;
    double craters = 0.0;
    double deadZones = 0.0;
    double volcanoes = 0.0;
    double arcVolcanoes = 0.0;
    double activeVolcanoes = 0.0; // within the radius of their danger
    double fault = 0.0;
    double majorFault = 0.0;
    double divergence = 0.0;
};

struct CellScores {
    double roughness = 0.0;
    double vegetation = 0.0;
    double arable = 0.0;
    double food = 0.0;
    double danger = 0.0;
    double prana = 0.0;
    double mana = 0.0;
    double habitability = 0.0;
    int model = 0;
};

// The class of a river of discharge q (0 below riverMin, 4 from impassableMin).
[[nodiscard]] int riverClass(double discharge, const RiverClasses& rivers);

[[nodiscard]] double roughnessOf(const RoughnessRule& rule, double sampleRange,
                                 double floorRange);
[[nodiscard]] double vegetationOf(const VegetationRule& rule, const CriterionValues& c);
// The index of the terrain model of the highest physical score (the first on a tie).
[[nodiscard]] int nearestModel(const SettlementConfig& config, const CriterionValues& c);
[[nodiscard]] double arableOf(const SettlementConfig& config, const CellInputs& in);
[[nodiscard]] double pranaOf(const PranaRule& rule, const CriterionValues& c, double vegetation);
[[nodiscard]] double foodOf(const FoodRule& rule, const CellInputs& in, double arable,
                            double vegetation, double prana);
[[nodiscard]] double dangerOf(const SettlementConfig& config, const CellInputs& in);
[[nodiscard]] double manaOf(const ManaRule& rule, const CellInputs& in);
[[nodiscard]] double habitabilityOf(const SettlementConfig& config, const CellInputs& in,
                                    const CellScores& s);
// The largest value habitabilityOf divides by: the positive weights of the terms that can reach
// 1 (the Aura has no map in V1).
[[nodiscard]] double habitabilityMax(const HabitabilityRule& rule);

// Every score of a cell; in.criteria gets its roughness.
[[nodiscard]] CellScores scoreCell(const SettlementConfig& config, CellInputs& in);

// The value of a source of the catalog on a cell, in [0, 1].
[[nodiscard]] double sourceValue(SourceKind kind, const CellInputs& in, const CellScores& s);
// A resource on a cell: its sources, at most 1, times its deposits (deposit01: its noise in
// [0, 1]).
[[nodiscard]] double resourceValue(const ResourceDef& r, const CellInputs& in,
                                   const CellScores& s, double deposit01);

} // namespace terrain::settlement
