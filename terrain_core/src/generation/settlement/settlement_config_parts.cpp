#include "settlement_config_parts.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace terrain::settlement {

namespace {

// Counts on the reference planet: at most this many of anything.
constexpr int kMaxCount = 1'000'000;

LineFamily readLineFamily(const Fields& f) {
    f.only({"count", "lengthKm", "widthKm", "intensity"});
    LineFamily l;
    l.count = readCountRange(f, "count", 0, 10'000);
    l.lengthKm = readRealRange(f, "lengthKm", 10.0, 1.0e6);
    l.widthKm = readRealRange(f, "widthKm", 1.0, 5000.0);
    l.intensity = readRealRange(f, "intensity", 0.0, 1.0);
    return l;
}

} // namespace

MagicRule readMagic(const Fields& f) {
    f.only({"majorLines", "minorLines", "turnKm", "turnDegrees", "nodes", "nodeRadiusKm",
            "nodeIntensity", "deadZones", "deadZoneRadiusKm"});
    MagicRule m;
    m.majorLines = readLineFamily(f.object("majorLines"));
    m.minorLines = readLineFamily(f.object("minorLines"));
    m.turnKm = f.numberIn("turnKm", 10.0, 1.0e6);
    m.turnDegrees = f.numberIn("turnDegrees", 0.0, 180.0);
    m.nodes = readCountRange(f, "nodes", 0, kMaxCount);
    m.nodeRadiusKm = readRealRange(f, "nodeRadiusKm", 1.0, 5000.0);
    m.nodeIntensity = readRealRange(f, "nodeIntensity", 0.0, 1.0);
    m.deadZones = readCountRange(f, "deadZones", 0, kMaxCount);
    m.deadZoneRadiusKm = readRealRange(f, "deadZoneRadiusKm", 1.0, 5000.0);
    return m;
}

GeologyRule readGeology(const Fields& f) {
    f.only({"faultWidthKm", "majorFaultWidthKm", "volcanoes", "activeVolcanoes",
            "largeVolcanoes", "arcDistanceKm", "riftShare", "hotspotShare", "volcanoSpacingKm",
            "volcanoInfluenceKm", "arcaneCraters", "majorCraters", "craterRadiusKm",
            "majorCraterRadiusKm"});
    GeologyRule g;
    g.faultWidthKm = f.numberIn("faultWidthKm", 1.0, 1000.0);
    g.majorFaultWidthKm = f.numberIn("majorFaultWidthKm", 1.0, 1000.0);
    g.volcanoes = readCountRange(f, "volcanoes", 0, kMaxCount);
    g.activeVolcanoes = readCountRange(f, "activeVolcanoes", 0, kMaxCount);
    g.largeVolcanoes = readCountRange(f, "largeVolcanoes", 0, kMaxCount);
    if (g.activeVolcanoes.max > g.volcanoes.min || g.largeVolcanoes.max > g.volcanoes.min) {
        f.fail("the active and the large volcanoes must be at most the volcanoes");
    }
    g.arcDistanceKm = readRealRange(f, "arcDistanceKm", 0.0, 2000.0);
    g.riftShare = f.numberIn("riftShare", 0.0, 1.0);
    g.hotspotShare = f.numberIn("hotspotShare", 0.0, 1.0);
    if (g.riftShare + g.hotspotShare > 1.0) {
        f.fail("riftShare + hotspotShare above 1");
    }
    g.volcanoSpacingKm = f.numberIn("volcanoSpacingKm", 0.0, 5000.0);
    g.volcanoInfluenceKm = f.numberIn("volcanoInfluenceKm", 1.0, 5000.0);
    g.arcaneCraters = readCountRange(f, "arcaneCraters", 0, kMaxCount);
    g.majorCraters = readCountRange(f, "majorCraters", 0, kMaxCount);
    if (g.majorCraters.max > g.arcaneCraters.min) {
        f.fail("the major craters must be at most the arcane craters");
    }
    g.craterRadiusKm = readRealRange(f, "craterRadiusKm", 1.0, 5000.0);
    g.majorCraterRadiusKm = readRealRange(f, "majorCraterRadiusKm", 1.0, 5000.0);
    return g;
}

HabitabilityRule readHabitability(const Fields& f) {
    f.only({"weights", "waterDistance", "climate", "terrain", "coastDistance",
            "riverMinDischarge"});
    HabitabilityRule h;
    const Fields w = f.object("weights");
    w.only({"water", "food", "climate", "terrain", "prana", "aura", "mana", "danger", "coast",
            "river"});
    h.water = w.numberIn("water", -10.0, 10.0);
    h.food = w.numberIn("food", -10.0, 10.0);
    h.climate = w.numberIn("climate", -10.0, 10.0);
    h.terrain = w.numberIn("terrain", -10.0, 10.0);
    h.prana = w.numberIn("prana", -10.0, 10.0);
    h.aura = w.numberIn("aura", -10.0, 10.0);
    h.mana = w.numberIn("mana", -10.0, 10.0);
    h.danger = w.numberIn("danger", -10.0, 10.0);
    h.coast = w.numberIn("coast", -10.0, 10.0);
    h.river = w.numberIn("river", -10.0, 10.0);
    h.waterDistance = readTrapezoid(f.object("waterDistance"));
    const Fields climate = f.object("climate");
    climate.only({"temperature", "rainfall"});
    h.climateTemperature = readTrapezoid(climate.object("temperature"));
    h.climateRainfall = readTrapezoid(climate.object("rainfall"));
    const Fields terrain = f.object("terrain");
    terrain.only({"roughness", "altitude"});
    h.terrainRoughness = readTrapezoid(terrain.object("roughness"));
    h.terrainAltitude = readTrapezoid(terrain.object("altitude"));
    h.coastDistance = readTrapezoid(f.object("coastDistance"));
    h.riverMinDischarge = f.numberIn("riverMinDischarge", 0.0, 1.0e7);
    return h;
}

std::vector<TerrainModel> readTerrainModels(const Fields& f) {
    std::vector<TerrainModel> models;
    const json::Value& v = f.value();
    if (v.size() == 0) {
        f.fail("at least one terrain model");
    }
    for (std::size_t k = 0; k < v.keys().size(); ++k) {
        const Fields m = f.object(v.keys()[k]);
        m.only({"expansionCost", "physicalPreferences", "physicalWeights"});
        TerrainModel model;
        model.id = v.keys()[k];
        model.expansionCost = m.numberIn("expansionCost", 0.1, 1000.0);
        model.preferences = readPreferences(m, Preferences{});
        if (model.preferences.empty()) {
            m.fail("a terrain model needs physicalPreferences");
        }
        models.push_back(std::move(model));
    }
    return models;
}

} // namespace terrain::settlement
