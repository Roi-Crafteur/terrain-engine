#include "settlement_config_parts.hpp"

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

} // namespace terrain::settlement
