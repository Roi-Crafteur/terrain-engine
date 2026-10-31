#include "terrain/generation/settlement/settlement_config.hpp"

#include "data_fields.hpp"
#include "settlement_config_parts.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace terrain::settlement {

namespace {

Trapezoid trapezoid(const Fields& f, std::string_view key) {
    return readTrapezoid(f.object(key));
}

RoughnessRule readRoughness(const Fields& f) {
    f.only({"sampleWeight", "sampleScaleM", "rangeWeight", "rangeScaleM"});
    RoughnessRule r;
    r.sampleWeight = f.numberIn("sampleWeight", 0.0, 1.0);
    r.sampleScale = f.numberIn("sampleScaleM", 1.0, 1.0e5);
    r.rangeWeight = f.numberIn("rangeWeight", 0.0, 1.0);
    r.rangeScale = f.numberIn("rangeScaleM", 1.0, 1.0e5);
    if (r.sampleWeight + r.rangeWeight > 1.0) {
        f.fail("sampleWeight + rangeWeight above 1");
    }
    return r;
}

VegetationRule readVegetation(const Fields& f) {
    f.only({"humidity", "temperature"});
    return VegetationRule{trapezoid(f, "humidity"), trapezoid(f, "temperature")};
}

RiverClasses readRivers(const Fields& f) {
    f.only({"riverMin", "largeMin", "majorMin", "impassableMin"});
    RiverClasses r;
    r.riverMin = f.numberIn("riverMin", 0.0, 1.0e7);
    r.largeMin = f.numberIn("largeMin", 0.0, 1.0e7);
    r.majorMin = f.numberIn("majorMin", 0.0, 1.0e7);
    r.impassableMin = f.numberIn("impassableMin", 0.0, 1.0e7);
    if (!(r.riverMin <= r.largeMin && r.largeMin <= r.majorMin &&
          r.majorMin <= r.impassableMin)) {
        f.fail("expected riverMin <= largeMin <= majorMin <= impassableMin");
    }
    return r;
}

ArableRule readArable(const Fields& f) {
    f.only({"temperature", "rainfall", "roughness", "alluvialBonus", "alluvialMinDischarge",
            "frozenSoil", "lakeSoil", "saltSoil"});
    ArableRule r;
    r.temperature = trapezoid(f, "temperature");
    r.rainfall = trapezoid(f, "rainfall");
    r.roughness = trapezoid(f, "roughness");
    r.alluvialBonus = f.numberIn("alluvialBonus", 0.0, 1.0);
    r.alluvialMinDischarge = f.numberIn("alluvialMinDischarge", 0.0, 1.0e7);
    r.frozenSoil = f.numberIn("frozenSoil", 0.0, 1.0);
    r.lakeSoil = f.numberIn("lakeSoil", 0.0, 1.0);
    r.saltSoil = f.numberIn("saltSoil", 0.0, 1.0);
    return r;
}

FoodRule readFood(const Fields& f) {
    f.only({"weights", "fishing", "herding"});
    FoodRule r;
    const Fields w = f.object("weights");
    w.only({"arable", "fishing", "herding", "hunting"});
    r.arable = w.numberIn("arable", 0.0, 10.0);
    r.fishing = w.numberIn("fishing", 0.0, 10.0);
    r.herding = w.numberIn("herding", 0.0, 10.0);
    r.hunting = w.numberIn("hunting", 0.0, 10.0);
    if (r.arable + r.fishing + r.herding + r.hunting <= 0.0) {
        w.fail("at least one weight must be above 0");
    }
    const Fields fishing = f.object("fishing");
    fishing.only({"coastDistance", "waterDistance", "freshShare"});
    r.fishingCoast = trapezoid(fishing, "coastDistance");
    r.fishingFresh = trapezoid(fishing, "waterDistance");
    r.freshShare = fishing.numberIn("freshShare", 0.0, 1.0);
    const Fields herding = f.object("herding");
    herding.only({"rainfall", "roughness", "temperature"});
    r.herdingRainfall = trapezoid(herding, "rainfall");
    r.herdingRoughness = trapezoid(herding, "roughness");
    r.herdingTemperature = trapezoid(herding, "temperature");
    return r;
}

DangerFactor readDangerFactor(const Fields& f) {
    f.only({"name", "weight", "ramps"});
    DangerFactor d;
    d.name = f.text("name");
    d.weight = f.numberIn("weight", 0.0, 10.0);
    const Fields ramps = f.object("ramps");
    const json::Value& v = ramps.value();
    if (v.size() == 0) {
        ramps.fail("at least one ramp");
    }
    for (std::size_t k = 0; k < v.keys().size(); ++k) {
        const std::optional<Criterion> c = criterionNamed(v.keys()[k]);
        if (!c) {
            ramps.fail(v.items()[k], v.keys()[k], "unknown criterion");
        }
        d.ramps.emplace_back(*c, readRamp(ramps.object(v.keys()[k])));
    }
    return d;
}

DangerRule readDanger(const Fields& f) {
    f.only({"factors", "volcano", "flood"});
    DangerRule r;
    const json::Value& factors = f.member("factors");
    if (!factors.isArray()) {
        f.fail(factors, "factors", "expected an array");
    }
    for (std::size_t k = 0; k < factors.size(); ++k) {
        r.factors.push_back(readDangerFactor(
            Fields(factors.items()[k], f.source(), f.pathOf("factors") + "[" +
                                                       std::to_string(k) + "]")));
    }
    const Fields volcano = f.object("volcano");
    volcano.only({"radiusKm", "weight"});
    r.volcanoRadiusKm = volcano.numberIn("radiusKm", 1.0, 1000.0);
    r.volcanoWeight = volcano.numberIn("weight", 0.0, 10.0);
    const Fields flood = f.object("flood");
    flood.only({"minDischarge", "altitude", "weight"});
    r.floodMinDischarge = flood.numberIn("minDischarge", 0.0, 1.0e7);
    r.floodAltitude = readRamp(flood.object("altitude"));
    r.floodWeight = flood.numberIn("weight", 0.0, 10.0);
    return r;
}

PranaRule readPrana(const Fields& f) {
    f.only({"vegetation", "freshWater", "biodiversity", "temperate", "sun", "aridity", "cold"});
    PranaRule r;
    const auto term = [&](std::string_view key, std::string_view input) {
        const Fields t = f.object(key);
        t.only({"weight", input});
        return std::pair<double, Fields>(t.numberIn("weight", -10.0, 10.0), t.object(input));
    };
    const Fields vegetation = f.object("vegetation");
    vegetation.only({"weight"});
    r.vegetation = vegetation.numberIn("weight", -10.0, 10.0);
    auto [fresh, freshRange] = term("freshWater", "waterDistance");
    r.freshWater = fresh;
    r.freshWaterDistance = readTrapezoid(freshRange);
    auto [bio, bioRange] = term("biodiversity", "temperature");
    r.biodiversity = bio;
    r.biodiversityTemperature = readTrapezoid(bioRange);
    auto [temperate, temperateRange] = term("temperate", "temperature");
    r.temperate = temperate;
    r.temperateTemperature = readTrapezoid(temperateRange);
    auto [sun, sunRange] = term("sun", "latitude");
    r.sun = sun;
    r.sunLatitude = readTrapezoid(sunRange);
    auto [arid, aridRamp] = term("aridity", "humidity");
    r.aridity = arid;
    r.aridityHumidity = readRamp(aridRamp);
    auto [cold, coldRamp] = term("cold", "temperature");
    r.cold = cold;
    r.coldTemperature = readRamp(coldRamp);
    return r;
}

ManaRule readMana(const Fields& f) {
    f.only({"lines", "nodes", "craters", "faults", "volcanoes", "deadZones"});
    ManaRule r;
    r.lines = f.numberIn("lines", 0.0, 10.0);
    r.nodes = f.numberIn("nodes", 0.0, 10.0);
    r.craters = f.numberIn("craters", 0.0, 10.0);
    r.faults = f.numberIn("faults", 0.0, 10.0);
    r.volcanoes = f.numberIn("volcanoes", 0.0, 10.0);
    r.deadZones = f.numberIn("deadZones", 0.0, 10.0);
    return r;
}

} // namespace

const TerrainModel* SettlementConfig::model(std::string_view id) const {
    for (const TerrainModel& m : terrainModels) {
        if (m.id == id) {
            return &m;
        }
    }
    return nullptr;
}

const WorldPreset* SettlementConfig::preset(std::string_view id) const {
    for (const WorldPreset& p : presets) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

SettlementConfig readSettlementConfig(const json::Value& document, std::string_view source) {
    const Fields f(document, std::string(source), "");
    f.only({"configVersion", "referencePlanetAreaKm2", "physicalScore", "roughness",
            "vegetation", "rivers", "arable", "food", "danger", "prana", "mana", "magic",
            "geology", "habitability", "suitability", "terrainModels", "worldPresets",
            "defaultPreset", "states", "capitals", "expansion", "borders", "goodWorld",
            "politics"});
    SettlementConfig c;
    c.version = f.text("configVersion");
    c.referenceAreaKm2 = f.numberIn("referencePlanetAreaKm2", 1.0, 1.0e12);
    const Fields score = f.object("physicalScore");
    score.only({"meanShare", "minShare"});
    c.physicalScore.meanShare = score.numberIn("meanShare", 0.0, 1.0);
    c.physicalScore.minShare = score.numberIn("minShare", 0.0, 1.0);
    c.roughness = readRoughness(f.object("roughness"));
    c.vegetation = readVegetation(f.object("vegetation"));
    c.rivers = readRivers(f.object("rivers"));
    c.arable = readArable(f.object("arable"));
    c.food = readFood(f.object("food"));
    c.danger = readDanger(f.object("danger"));
    c.prana = readPrana(f.object("prana"));
    c.mana = readMana(f.object("mana"));
    c.magic = readMagic(f.object("magic"));
    c.geology = readGeology(f.object("geology"));
    c.habitability = readHabitability(f.object("habitability"));
    const Fields suitability = f.object("suitability");
    suitability.only({"noiseAmplitude", "noiseScaleKm"});
    c.suitability.noiseAmplitude = suitability.numberIn("noiseAmplitude", 0.0, 1.0);
    c.suitability.noiseScaleKm = suitability.numberIn("noiseScaleKm", 1.0, 1.0e5);
    c.terrainModels = readTerrainModels(f.object("terrainModels"));
    c.presets = readPresets(f.object("worldPresets"));
    c.defaultPreset = f.text("defaultPreset");
    if (c.preset(c.defaultPreset) == nullptr) {
        f.fail(f.member("defaultPreset"), "defaultPreset",
               "unknown preset '" + c.defaultPreset + "'");
    }
    c.states = readStates(f.object("states"));
    c.capitals = readCapitals(f.object("capitals"));
    c.expansion = readExpansion(f.object("expansion"));
    for (const std::string& id : c.expansion.barrierModels) {
        if (c.model(id) == nullptr) {
            const Fields e = f.object("expansion");
            e.fail(e.member("barrierModels"), "barrierModels", "unknown terrain model '" + id + "'");
        }
    }
    c.borders = readBorders(f.object("borders"));
    c.goodWorld = readGoodWorld(f.object("goodWorld"), c.terrainModels);
    c.politics = readPolitics(f.object("politics"));
    c.document = document;
    return c;
}

} // namespace terrain::settlement
