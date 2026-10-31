#include "analysis_cell.hpp"

#include "terrain/generation/noise.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"

#include <algorithm>
#include <cstddef>

namespace terrain::settlement {

namespace {

double value(const CriterionValues& c, Criterion k) {
    return c[static_cast<std::size_t>(k)];
}

double trap(const CriterionValues& c, Criterion k, const Trapezoid& t) {
    return trapezoidScore(value(c, k), t);
}

// The definitions of the sources that are not a map of their own: the bands of the intensity of
// the relief of the geography (from its floor 0.03 to 1) for the mountains, the old massifs (worn)
// and the hills; the dry coasts; the dry closed basins.
constexpr Ramp kMountainRelief{0.35, 0.8};
constexpr Ramp kOldMountainRelief{0.25, 0.7};
constexpr Trapezoid kHillsRelief{-kInfinity, 0.08, 0.18, 0.35, 0.5, kInfinity, 0.0};
constexpr Ramp kAridCoastKm{30.0, 0.0};
constexpr Ramp kAridHumidity{0.35, 0.15};
constexpr Ramp kClosedHumidity{0.45, 0.2};

} // namespace

int riverClass(double discharge, const RiverClasses& rivers) {
    if (discharge >= rivers.impassableMin) {
        return 4;
    }
    if (discharge >= rivers.majorMin) {
        return 3;
    }
    if (discharge >= rivers.largeMin) {
        return 2;
    }
    return discharge >= rivers.riverMin ? 1 : 0;
}

double roughnessOf(const RoughnessRule& rule, double sampleRange, double floorRange) {
    return std::clamp(rule.sampleWeight * std::min(1.0, sampleRange / rule.sampleScale) +
                          rule.rangeWeight * std::min(1.0, floorRange / rule.rangeScale),
                      0.0, 1.0);
}

double vegetationOf(const VegetationRule& rule, const CriterionValues& c) {
    return trap(c, Criterion::Humidity, rule.humidity) *
           trap(c, Criterion::Temperature, rule.temperature);
}

int nearestModel(const SettlementConfig& config, const CriterionValues& c) {
    int best = 0;
    double bestScore = -1.0;
    for (std::size_t m = 0; m < config.terrainModels.size(); ++m) {
        const double s = physicalScore(config.terrainModels[m].preferences, c, config.physicalScore);
        if (s > bestScore) {
            bestScore = s;
            best = static_cast<int>(m);
        }
    }
    return best;
}

double arableOf(const SettlementConfig& config, const CellInputs& in) {
    const ArableRule& r = config.arable;
    const CriterionValues& c = in.criteria;
    double soil = 1.0;
    if ((in.water & kFrozenBit) != 0) {
        soil = r.frozenSoil;
    } else if ((in.water & kSaltLakeBit) != 0) {
        soil = r.saltSoil;
    } else if ((in.water & kFreshLakeBit) != 0) {
        soil = r.lakeSoil;
    }
    const double temperature = trap(c, Criterion::Temperature, r.temperature);
    const double flat = trap(c, Criterion::Roughness, r.roughness);
    double arable = temperature * trap(c, Criterion::Rainfall, r.rainfall) * flat * soil;
    // The floodplains of the large rivers, even in a desert (the Nile), never frozen nor steep.
    if (in.riverNear >= riverClass(r.alluvialMinDischarge, config.rivers) && soil > 0.0 &&
        temperature > 0.0) {
        arable += r.alluvialBonus * flat;
    }
    return std::min(arable, 1.0);
}

double pranaOf(const PranaRule& r, const CriterionValues& c, double vegetation) {
    const double terms[] = {
        r.vegetation * vegetation,
        r.freshWater * trap(c, Criterion::WaterDistance, r.freshWaterDistance),
        r.biodiversity * vegetation * trap(c, Criterion::Temperature, r.biodiversityTemperature),
        r.temperate * trap(c, Criterion::Temperature, r.temperateTemperature),
        r.sun * trap(c, Criterion::Latitude, r.sunLatitude),
        r.aridity * rampScore(value(c, Criterion::Humidity), r.aridityHumidity),
        r.cold * rampScore(value(c, Criterion::Temperature), r.coldTemperature),
    };
    const double weights[] = {r.vegetation, r.freshWater, r.biodiversity, r.temperate,
                              r.sun,        r.aridity,    r.cold};
    double sum = 0.0;
    double positive = 0.0;
    for (std::size_t k = 0; k < std::size(terms); ++k) {
        sum += terms[k];
        positive += std::max(weights[k], 0.0);
    }
    return positive > 0.0 ? std::clamp(sum / positive, 0.0, 1.0) : 0.0;
}

double foodOf(const FoodRule& r, const CellInputs& in, double arable, double vegetation,
              double prana) {
    const CriterionValues& c = in.criteria;
    const double fishing =
        std::max(trap(c, Criterion::CoastDistance, r.fishingCoast),
                 r.freshShare * trap(c, Criterion::WaterDistance, r.fishingFresh));
    const double herding = trap(c, Criterion::Rainfall, r.herdingRainfall) *
                           trap(c, Criterion::Roughness, r.herdingRoughness) *
                           trap(c, Criterion::Temperature, r.herdingTemperature);
    const double hunting = vegetation * prana;
    const double total = r.arable + r.fishing + r.herding + r.hunting;
    return (r.arable * arable + r.fishing * fishing + r.herding * herding + r.hunting * hunting) /
           total;
}

double dangerOf(const SettlementConfig& config, const CellInputs& in) {
    const DangerRule& r = config.danger;
    double danger = 0.0;
    for (const DangerFactor& f : r.factors) {
        double product = f.weight;
        for (const auto& [criterion, ramp] : f.ramps) {
            product *= rampScore(value(in.criteria, criterion), ramp);
        }
        danger += product;
    }
    danger += r.volcanoWeight * in.activeVolcanoes;
    if (in.riverNear >= riverClass(r.floodMinDischarge, config.rivers)) {
        danger += r.floodWeight * rampScore(value(in.criteria, Criterion::Altitude), r.floodAltitude);
    }
    return std::clamp(danger, 0.0, 1.0);
}

double manaOf(const ManaRule& r, const CellInputs& in) {
    const double mana = r.lines * in.lines + r.nodes * in.nodes + r.craters * in.craters +
                        r.faults * in.fault + r.volcanoes * in.volcanoes -
                        r.deadZones * in.deadZones;
    return std::clamp(mana, 0.0, 1.0);
}

double habitabilityMax(const HabitabilityRule& r) {
    const double positive[] = {r.water, r.food,  r.climate, r.terrain,
                               r.prana, r.mana,  r.coast,   r.river};
    double max = 0.0;
    for (const double w : positive) {
        max += std::max(w, 0.0);
    }
    return max;
}

double habitabilityOf(const SettlementConfig& config, const CellInputs& in, const CellScores& s) {
    const HabitabilityRule& r = config.habitability;
    const CriterionValues& c = in.criteria;
    const double climate = trap(c, Criterion::Temperature, r.climateTemperature) *
                           trap(c, Criterion::Rainfall, r.climateRainfall);
    const double terrain = trap(c, Criterion::Roughness, r.terrainRoughness) *
                           trap(c, Criterion::Altitude, r.terrainAltitude);
    const double river =
        in.riverNear >= riverClass(r.riverMinDischarge, config.rivers) ? 1.0 : 0.0;
    const double sum = r.water * trap(c, Criterion::WaterDistance, r.waterDistance) +
                       r.food * s.food + r.climate * climate + r.terrain * terrain +
                       r.prana * s.prana + r.aura * 0.0 + r.mana * s.mana + r.danger * s.danger +
                       r.coast * trap(c, Criterion::CoastDistance, r.coastDistance) +
                       r.river * river;
    const double max = habitabilityMax(r);
    return max > 0.0 ? std::clamp(sum / max, 0.0, 1.0) : 0.0;
}

CellScores scoreCell(const SettlementConfig& config, CellInputs& in) {
    CellScores s;
    s.roughness = roughnessOf(config.roughness, in.sampleRange, in.floorRange);
    in.criteria[static_cast<std::size_t>(Criterion::Roughness)] = s.roughness;
    s.vegetation = vegetationOf(config.vegetation, in.criteria);
    s.model = nearestModel(config, in.criteria);
    s.arable = arableOf(config, in);
    s.prana = pranaOf(config.prana, in.criteria, s.vegetation);
    s.food = foodOf(config.food, in, s.arable, s.vegetation, s.prana);
    s.danger = dangerOf(config, in);
    s.mana = manaOf(config.mana, in);
    s.habitability = habitabilityOf(config, in, s);
    return s;
}

double sourceValue(SourceKind kind, const CellInputs& in, const CellScores& s) {
    const CriterionValues& c = in.criteria;
    switch (kind) {
    case SourceKind::ForestCover:
        return s.vegetation;
    case SourceKind::Roughness:
        return s.roughness;
    case SourceKind::Mountain:
        return rampScore(in.relief, kMountainRelief);
    case SourceKind::OldMountain:
        return rampScore(in.relief, kOldMountainRelief) * (1.0 - in.character);
    case SourceKind::Hills:
        return trapezoidScore(in.relief, kHillsRelief);
    case SourceKind::Collision:
        return in.belt;
    case SourceKind::Divergence:
        return in.divergence;
    case SourceKind::Transform:
    case SourceKind::MajorFault:
        return in.majorFault;
    case SourceKind::Fault:
        return in.fault;
    case SourceKind::Volcano:
        return in.volcanoes;
    case SourceKind::VolcanicArc:
        return in.arcVolcanoes;
    case SourceKind::AridCoast:
        return rampScore(value(c, Criterion::CoastDistance), kAridCoastKm) *
               rampScore(value(c, Criterion::Humidity), kAridHumidity);
    case SourceKind::SaltLake:
        return (in.water & kSaltLakeBit) != 0 || in.saltNear ? 1.0 : 0.0;
    case SourceKind::ClosedBasin:
        return (in.water & kClosedBasinBit) != 0
                   ? rampScore(value(c, Criterion::Humidity), kClosedHumidity)
                   : 0.0;
    case SourceKind::ManaNode:
        return in.nodes;
    case SourceKind::Crater:
        return in.craters;
    }
    return 0.0;
}

double resourceValue(const ResourceDef& r, const CellInputs& in, const CellScores& s,
                     double deposit01) {
    double sum = 0.0;
    for (const ResourceSource& source : r.sources) {
        sum += source.weight * sourceValue(source.kind, in, s);
    }
    const auto threshold = static_cast<float>(r.depositThreshold);
    const double deposit = noise::smoothstep(threshold,
                                             threshold + static_cast<float>(r.depositSoftness),
                                             static_cast<float>(deposit01));
    return std::min(sum, 1.0) * deposit;
}

} // namespace terrain::settlement
