#include "settlement_config_parts.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace terrain::settlement {

namespace {

// {"mode": "range", "min", "max"} or {"mode": "exact", "value"}, in [low, high]; whole numbers
// when whole.
PresetValue readPresetValue(const Fields& parent, std::string_view key, double low, double high,
                            bool whole) {
    const Fields f = parent.object(key);
    const std::string mode = f.text("mode");
    const auto read = [&](std::string_view k) {
        return whole ? static_cast<double>(f.integer(k, static_cast<int>(low),
                                                     static_cast<int>(high)))
                     : f.numberIn(k, low, high);
    };
    PresetValue v;
    if (mode == "exact") {
        f.only({"mode", "value"});
        v.exact = true;
        v.min = read("value");
        v.max = v.min;
    } else if (mode == "range") {
        f.only({"mode", "min", "max"});
        v.min = read("min");
        v.max = read("max");
        if (v.min > v.max) {
            f.fail("min is above max");
        }
    } else {
        f.fail(f.member("mode"), "mode", "expected \"range\" or \"exact\"");
    }
    return v;
}

CostPenalty readPenalty(const Fields& parent, std::string_view key) {
    const Fields f = parent.object(key);
    f.only({"from", "to", "cost"});
    CostPenalty p;
    p.ramp = Ramp{f.number("from"), f.number("to")};
    p.cost = f.numberIn("cost", 0.0, 1000.0);
    if (p.ramp.from == p.ramp.to) {
        f.fail("from equals to");
    }
    return p;
}

} // namespace

std::vector<WorldPreset> readPresets(const Fields& f) {
    std::vector<WorldPreset> presets;
    const json::Value& v = f.value();
    if (v.size() == 0) {
        f.fail("at least one preset");
    }
    for (std::size_t k = 0; k < v.keys().size(); ++k) {
        const Fields p = f.object(v.keys()[k]);
        p.only({"civilizedShare", "majorKingdoms", "minorStates"});
        WorldPreset preset;
        preset.id = v.keys()[k];
        preset.civilizedShare = readPresetValue(p, "civilizedShare", 0.0, 1.0, false);
        preset.majorKingdoms = readPresetValue(p, "majorKingdoms", 0.0, 100'000.0, true);
        preset.minorStates = readPresetValue(p, "minorStates", 0.0, 100'000.0, true);
        presets.push_back(std::move(preset));
    }
    return presets;
}

StateRule readStates(const Fields& f) {
    f.only({"majorAreaShare", "areaSpread", "maxLandShare"});
    StateRule r;
    r.majorAreaShare = f.numberIn("majorAreaShare", 0.0, 1.0);
    r.areaSpread = f.numberIn("areaSpread", 0.0, 0.9);
    r.maxLandShare = f.numberIn("maxLandShare", 0.0, 1.0);
    return r;
}

CapitalRule readCapitals(const Fields& f) {
    f.only({"topHabitability", "maxFreshWaterKm", "seedRadius", "spacing", "relax",
            "relaxSteps"});
    CapitalRule r;
    r.topHabitability = f.numberIn("topHabitability", 0.0, 1.0);
    r.maxFreshWaterKm = f.numberIn("maxFreshWaterKm", 0.0, 250.0);
    r.seedRadius = f.integer("seedRadius", 1, 16);
    r.spacing = f.numberIn("spacing", 0.0, 10.0);
    r.relax = f.numberIn("relax", 0.0, 1.0);
    r.relaxSteps = f.integer("relaxSteps", 0, 16);
    return r;
}

ExpansionRule readExpansion(const Fields& f) {
    f.only({"preferenceWeight", "minCost", "penalties", "riverCrossing", "barrierRiverClass",
            "barrierModels", "fordBonus", "fordMaxRoughness", "confluenceBonus", "lakeCost", "ridgeCost", "sea",
            "seaCost", "straitCells", "minSuitability", "maxCost", "overflow", "maxPeopleShare",
            "peopleReruns"});
    ExpansionRule r;
    r.preferenceWeight = f.numberIn("preferenceWeight", 0.0, 0.6);
    r.minCost = f.numberIn("minCost", 0.01, 100.0);
    const Fields penalties = f.object("penalties");
    penalties.only({"altitude", "roughness", "aridity", "danger"});
    r.altitude = readPenalty(penalties, "altitude");
    r.roughness = readPenalty(penalties, "roughness");
    r.aridity = readPenalty(penalties, "aridity");
    r.danger = penalties.numberIn("danger", 0.0, 1000.0);
    const Fields crossing = f.object("riverCrossing");
    crossing.only({"river", "largeRiver", "majorRiver", "impassableRiver"});
    r.riverCrossing = {crossing.numberIn("river", 0.0, 1000.0),
                       crossing.numberIn("largeRiver", 0.0, 1000.0),
                       crossing.numberIn("majorRiver", 0.0, 1000.0),
                       crossing.numberIn("impassableRiver", 0.0, 1000.0)};
    r.barrierRiverClass = f.integer("barrierRiverClass", 1, 5);
    r.barrierModels = f.strings("barrierModels");
    r.fordBonus = f.numberIn("fordBonus", 0.0, 1.0);
    r.fordMaxRoughness = f.numberIn("fordMaxRoughness", 0.01, 1.0);
    r.confluenceBonus = f.numberIn("confluenceBonus", 0.0, 1000.0);
    r.lakeCost = f.numberIn("lakeCost", 0.0, 1000.0);
    r.ridgeCost = f.numberIn("ridgeCost", 0.0, 1000.0);
    const std::string sea = f.text("sea");
    if (sea != "straits" && sea != "forbidden") {
        f.fail(f.member("sea"), "sea", "expected \"straits\" or \"forbidden\"");
    }
    r.sea = sea == "straits" ? SeaCrossing::Straits : SeaCrossing::Forbidden;
    r.seaCost = f.numberIn("seaCost", 0.0, 10'000.0);
    r.straitCells = f.integer("straitCells", 0, 16);
    r.minSuitability = f.numberIn("minSuitability", 0.0, 1.0);
    r.maxCost = f.numberIn("maxCost", 1.0, 1.0e7);
    r.overflow = f.numberIn("overflow", 1.0, 10.0);
    r.maxPeopleShare = f.numberIn("maxPeopleShare", 0.0, 1.0);
    r.peopleReruns = f.integer("peopleReruns", 0, 16);
    return r;
}

BorderRule readBorders(const Fields& f) {
    f.only({"ridgeHeightM", "ridgeWindow", "ridgeRelief", "coastKm", "holeCells", "cutFactor",
            "cutMinMm"});
    BorderRule r;
    r.ridgeHeightM = f.numberIn("ridgeHeightM", 0.0, 10'000.0);
    r.ridgeWindow = f.integer("ridgeWindow", 1, 8);
    r.ridgeRelief = f.numberIn("ridgeRelief", 0.0, 1.0);
    r.coastKm = f.numberIn("coastKm", 0.0, 255.0);
    r.holeCells = f.integer("holeCells", 0, 10'000);
    r.cutFactor = f.numberIn("cutFactor", 1.0, 1000.0);
    r.cutMinMm = f.numberIn("cutMinMm", 0.0, 10'000.0);
    return r;
}

GoodWorldRule readGoodWorld(const Fields& f, const std::vector<TerrainModel>& models) {
    f.only({"hostileModels", "highMountainM", "likedSuitability"});
    GoodWorldRule r;
    r.hostileModels = f.strings("hostileModels");
    for (const std::string& id : r.hostileModels) {
        if (std::none_of(models.begin(), models.end(),
                         [&](const TerrainModel& m) { return m.id == id; })) {
            f.fail(f.member("hostileModels"), "hostileModels", "unknown terrain model '" + id + "'");
        }
    }
    r.highMountainM = f.numberIn("highMountainM", 0.0, 20'000.0);
    r.likedSuitability = f.numberIn("likedSuitability", 0.0, 1.0);
    return r;
}

} // namespace terrain::settlement
