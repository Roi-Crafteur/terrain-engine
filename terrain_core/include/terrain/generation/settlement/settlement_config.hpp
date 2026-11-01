#pragma once

#include "terrain/generation/settlement/kingdom_rules.hpp"
#include "terrain/generation/settlement/preference.hpp"
#include "terrain/json/json.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The configuration of the settlement of a world (docs/PEUPLEMENT.md, section 6 bis): every
// weight of the analysis maps, of the energies and of the habitability, the counts of the
// geology and of the magic, the terrain models, the presets and the rules of the kingdoms. Read from data/settlement/world.json, never
// coded: changing a weight needs no agent. Lengths in km, altitudes in m, temperatures in degrees
// Celsius, rainfall in mm/yr, discharges in m^3/s.
namespace terrain::settlement {

struct RealRange {
    double min = 0.0;
    double max = 0.0;
};

// A count on the reference planet (SettlementConfig::referenceAreaKm2), drawn from the seed in
// [min, max] and scaled to the area of the world.
struct CountRange {
    int min = 0;
    int max = 0;
};

// Roughness of a cell of 8 km (not a mean slope): sampleWeight times the range of the heights of
// 4 points of the relief inside the cell (at 0.3 of its side from its center) over sampleScale
// meters, plus rangeWeight times the range of the floors of the 3 x 3 cells around it over
// rangeScale meters, each term at most its weight. Rank correlation 0.92 with the spread of the
// real heights in the cell on the default world (the floors alone: 0.75).
struct RoughnessRule {
    double sampleWeight = 0.8;
    double sampleScale = 800.0;
    double rangeWeight = 0.2;
    double rangeScale = 1500.0;
};

// The cover of the vegetation (the forests at the scale of the planet): read on the climate,
// never on the biome at a point.
struct VegetationRule {
    Trapezoid humidity;
    Trapezoid temperature;
};

// The classes of the rivers by discharge (section 6 bis): river from riverMin, large river from
// largeMin, major river from majorMin, impassable from impassableMin.
struct RiverClasses {
    double riverMin = 10.0;
    double largeMin = 100.0;
    double majorMin = 1000.0;
    double impassableMin = 10000.0;
};

// Arable land (section 4 ter): temperature x rainfall x roughness x soil + alluvialBonus within
// one cell of a river of at least alluvialMinDischarge, at most 1. The soil is frozenSoil on a
// frozen cell, lakeSoil under a lake, saltSoil on a salt lake or playa, 1 elsewhere.
struct ArableRule {
    Trapezoid temperature;
    Trapezoid rainfall;
    Trapezoid roughness;
    double alluvialBonus = 0.2;
    double alluvialMinDischarge = 100.0;
    double frozenSoil = 0.0;
    double lakeSoil = 0.0;
    double saltSoil = 0.1;
};

// Food (section 4 ter): arable x arable + fishing x fishing + herding x herding + hunting x
// hunting, over the sum of the weights. Fishing: the coast, or freshShare of the fresh water;
// herding: grassland, hills, steppes; hunting and gathering: the vegetation times the Prana.
struct FoodRule {
    double arable = 1.0;
    double fishing = 0.4;
    double herding = 0.4;
    double hunting = 0.3;
    Trapezoid fishingCoast;
    Trapezoid fishingFresh;
    double freshShare = 0.7;
    Trapezoid herdingRainfall;
    Trapezoid herdingRoughness;
    Trapezoid herdingTemperature;
};

// A factor of the natural danger: weight times the product of its ramps.
struct DangerFactor {
    std::string name;
    double weight = 0.0;
    std::vector<std::pair<Criterion, Ramp>> ramps;
};

// Natural danger: the sum of the factors, of the active volcanoes (volcanoWeight within
// volcanoRadiusKm, fading to 0 there) and of the floods (floodWeight on the cells within one cell
// of a river of at least floodMinDischarge, times floodAltitude), at most 1.
struct DangerRule {
    std::vector<DangerFactor> factors;
    double volcanoRadiusKm = 50.0;
    double volcanoWeight = 0.6;
    double floodMinDischarge = 1000.0;
    Ramp floodAltitude;
    double floodWeight = 0.3;
};

// Prana (section 4): vegetation + fresh water + biodiversity (the vegetation in the warmth) +
// temperate climate + sun - aridity - extreme cold, over the sum of the positive weights,
// clamped to [0, 1].
struct PranaRule {
    double vegetation = 0.35;
    double freshWater = 0.15;
    Trapezoid freshWaterDistance;
    double biodiversity = 0.2;
    Trapezoid biodiversityTemperature;
    double temperate = 0.15;
    Trapezoid temperateTemperature;
    double sun = 0.15;
    Trapezoid sunLatitude;
    double aridity = -0.25;
    Ramp aridityHumidity;
    double cold = -0.25;
    Ramp coldTemperature;
};

// Mana (section 4): lines x lines + nodes x nodes + craters x craters + faults x faults +
// volcanoes x volcanoes - deadZones x deadZones, clamped to [0, 1] once known.
struct ManaRule {
    double lines = 1.0;
    double nodes = 1.0;
    double craters = 0.8;
    double faults = 0.2;
    double volcanoes = 0.15;
    double deadZones = 1.0;
};

// A family of telluric lines: long curves of the seed, independent of the plates and of the relief
// (section 4). Their heading wanders by up to turnDegrees over turnKm.
struct LineFamily {
    CountRange count;
    RealRange lengthKm;
    RealRange widthKm;
    RealRange intensity;
};

struct MagicRule {
    LineFamily majorLines;
    LineFamily minorLines;
    double turnKm = 2500.0;
    double turnDegrees = 50.0;
    // Nodes: at the crossings of the lines first, then along the lines up to the count.
    CountRange nodes;
    RealRange nodeRadiusKm;
    RealRange nodeIntensity;
    // Dead zones: blobs where the Mana falls to 0.
    CountRange deadZones;
    RealRange deadZoneRadiusKm;
};

// Geology in data, phase 1 (section 1): faults along the boundaries of the plates (the transform
// boundaries are the major faults), volcanoes in the arcs of the subductions, along the rifts and
// the ridges reaching the land, and at hotspots; arcane craters anywhere. Data only: the relief
// is never changed.
struct GeologyRule {
    double faultWidthKm = 40.0;
    double majorFaultWidthKm = 80.0;
    CountRange volcanoes;
    CountRange activeVolcanoes;
    CountRange largeVolcanoes;
    RealRange arcDistanceKm;
    double riftShare = 0.15;
    double hotspotShare = 0.05;
    double volcanoSpacingKm = 25.0;
    double volcanoInfluenceKm = 60.0;
    CountRange arcaneCraters;
    CountRange majorCraters;
    RealRange craterRadiusKm;
    RealRange majorCraterRadiusKm;
};

// Habitability (section 6 bis): the weighted sum of its terms, over its theoretical maximum (the
// sum of the positive weights times the largest value of each term), clamped to [0, 1]. No Aura
// map in V1: its largest value is 0.
struct HabitabilityRule {
    double water = 1.0;
    double food = 1.0;
    double climate = 0.8;
    double terrain = 0.6;
    double prana = 0.3;
    double aura = 0.2;
    double mana = 0.4;
    double danger = -1.0;
    double coast = 0.2;
    double river = 0.5;
    Trapezoid waterDistance;
    Trapezoid climateTemperature;
    Trapezoid climateRainfall;
    Trapezoid terrainRoughness;
    Trapezoid terrainAltitude;
    Trapezoid coastDistance;
    double riverMinDischarge = 100.0;
};

// raceSuitability (section 6): the smooth noise of the seed added to the score of each people,
// its own field for each, of this amplitude (a share of the theoretical maximum) and scale.
struct SuitabilityRule {
    double noiseAmplitude = 0.2;
    double noiseScaleKm = 400.0;
};

// A terrain model (section 6): physical preferences that a people takes as its base, and the base
// cost of the expansion over a cell whose nearest model it is.
struct TerrainModel {
    std::string id;
    double expansionCost = 1.0;
    Preferences preferences;
};

struct SettlementConfig {
    std::string version;
    double referenceAreaKm2 = 2.1e9;
    PhysicalScoreRule physicalScore;
    RoughnessRule roughness;
    VegetationRule vegetation;
    RiverClasses rivers;
    ArableRule arable;
    FoodRule food;
    DangerRule danger;
    PranaRule prana;
    ManaRule mana;
    MagicRule magic;
    GeologyRule geology;
    HabitabilityRule habitability;
    SuitabilityRule suitability;
    std::vector<TerrainModel> terrainModels; // in the order of the file
    std::vector<WorldPreset> presets;        // in the order of the file
    std::string defaultPreset;               // one of presets
    StateRule states;
    CapitalRule capitals;
    ExpansionRule expansion;
    BorderRule borders;
    GoodWorldRule goodWorld;
    PoliticsRule politics;
    // The document read, kept whole for the copy of the world.
    json::Value document;

    // The model of that id, or nullptr.
    [[nodiscard]] const TerrainModel* model(std::string_view id) const;
    // The preset of that id, or nullptr.
    [[nodiscard]] const WorldPreset* preset(std::string_view id) const;
};

// The configuration of a document (the world.json of a data folder, or the copy kept with a
// world). Throws json::Error "<source>: <path>: <reason> (line L, column C)" for anything missing,
// unknown, out of range or inconsistent.
[[nodiscard]] SettlementConfig readSettlementConfig(const json::Value& document,
                                                    std::string_view source);

} // namespace terrain::settlement
