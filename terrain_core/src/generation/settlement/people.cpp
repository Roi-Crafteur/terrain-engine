#include "terrain/generation/settlement/people.hpp"

#include "data_fields.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr std::array<std::string_view, 5> kNeeds = {"freshwater", "farmland", "food", "mana",
                                                    "prana"};
constexpr std::array<std::string_view, 4> kFears = {"highMagic", "volcano", "deepOcean",
                                                    "monsterDensity"};
constexpr std::array<std::string_view, 10> kUnderground = {
    "caveDensity", "stableRock", "groundwater",      "ore",             "stone",
    "heat",        "cold",       "volcanoProximity", "dragonProximity", "surfaceAccess"};

template <std::size_t N>
bool among(std::string_view key, const std::array<std::string_view, N>& keys) {
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

// An object of numbers in [low, high]; when known is given, its keys must be among them.
template <std::size_t N>
Weights readWeights(const Fields& parent, std::string_view key, double low, double high,
                    const std::array<std::string_view, N>* known) {
    Weights out;
    const std::optional<Fields> f = parent.optionalObject(key);
    if (!f) {
        return out;
    }
    const json::Value& v = f->value();
    for (std::size_t k = 0; k < v.keys().size(); ++k) {
        if (known != nullptr && !among(v.keys()[k], *known)) {
            std::string list;
            for (const std::string_view a : *known) {
                list += list.empty() ? "" : ", ";
                list += a;
            }
            f->fail(v.items()[k], v.keys()[k], "unknown key (expected one of: " + list + ")");
        }
        out.emplace_back(v.keys()[k], f->numberIn(v.keys()[k], low, high));
    }
    return out;
}

void readUnderground(const Fields& f, People& p) {
    const std::optional<Fields> u = f.optionalObject("underground");
    if (!u) {
        return;
    }
    u->only({"level", "preferredDepth", "needsSurfaceAccess"});
    const std::string level = u->text("level");
    constexpr std::array<std::string_view, 4> kLevels = {"none", "partial", "mostly", "full"};
    const auto it = std::find(kLevels.begin(), kLevels.end(), level);
    if (it == kLevels.end()) {
        u->fail(u->member("level"), "level", "expected none, partial, mostly or full");
    }
    p.underground = static_cast<Underground>(it - kLevels.begin());
    p.preferredDepth = u->numberIn("preferredDepth", 0.0, 1.0, 0.0);
    p.needsSurfaceAccess = u->flag("needsSurfaceAccess", true);
}

void readEnergies(const Fields& f, People& p) {
    const std::optional<Fields> e = f.optionalObject("ambientEnergyPreferences");
    if (!e) {
        return;
    }
    e->only({"prana", "aura", "mana"});
    p.prana = e->numberIn("prana", -1.0, 1.0, 0.0);
    p.aura = e->numberIn("aura", -1.0, 1.0, 0.0);
    p.mana = e->numberIn("mana", -1.0, 1.0, 0.0);
}

void readRulers(const Fields& f, People& p) {
    if (const std::optional<Fields> t = f.optionalObject("rulerTraits")) {
        const json::Value& v = t->value();
        for (std::size_t k = 0; k < v.keys().size(); ++k) {
            const auto it = std::find(kRulerTraits.begin(), kRulerTraits.end(), v.keys()[k]);
            if (it == kRulerTraits.end()) {
                t->fail(v.items()[k], v.keys()[k],
                        "unknown trait (expected ambition, aggression, greed, prudence, piety)");
            }
            const RealRange r = readRealRange(*t, v.keys()[k], 0.0, 1.0);
            p.traits[static_cast<std::size_t>(it - kRulerTraits.begin())] = {r.min, r.max};
        }
    }
    const Fields names = f.object("names");
    names.only({"syllables", "titles"});
    p.syllables = names.strings("syllables");
    if (p.syllables.size() < 2) {
        names.fail(names.member("syllables"), "syllables", "at least 2 syllables");
    }
    if (const std::optional<Fields> titles = names.optionalObject("titles")) {
        const json::Value& v = titles->value();
        for (std::size_t k = 0; k < v.keys().size(); ++k) {
            p.titles.emplace_back(v.keys()[k], titles->text(v.keys()[k]));
        }
    }
}

void readSuitability(const Fields& f, People& p) {
    p.habitabilityWeight = f.numberIn("habitabilityWeight", 0.0, 10.0, 1.0);
    p.physicalWeight = f.numberIn("physicalWeight", 0.0, 10.0, 1.0);
    p.dangerSensitivity = f.numberIn("dangerSensitivity", 0.0, 10.0, 0.5);
}

} // namespace

double People::relationTo(std::string_view other) const {
    if (other == id) {
        return selfRelation;
    }
    for (const auto& [key, value] : relations) {
        if (key == other) {
            return value;
        }
    }
    return 0.0;
}

std::string People::titleOf(std::string_view kind) const {
    for (const auto& [key, value] : titles) {
        if (key == kind) {
            return value;
        }
    }
    return "";
}

People readPeople(const json::Value& document, std::string_view source,
                  const SettlementConfig& config) {
    const Fields f(document, std::string(source), "");
    f.only({"id", "type", "name", "plane", "populationShare", "majorKingdomShare",
            "minorStateShare", "baseTerrainModel", "physicalPreferences", "physicalWeights",
            "habitabilityWeight", "physicalWeight", "dangerSensitivity",
            "ambientEnergyPreferences", "underground", "undergroundPreferences", "needs", "fears",
            "government", "relations", "selfRelation", "rulerTraits", "names"});
    People p;
    p.id = f.text("id");
    if (p.id.empty()) {
        f.fail(f.member("id"), "id", "empty");
    }
    if (f.text("type") != "people") {
        f.fail(f.member("type"), "type", "expected \"people\"");
    }
    p.name = f.text("name", p.id);
    p.plane = f.text("plane", "material");
    p.populationShare = f.numberIn("populationShare", 0.0, 1.0e6, 0.0);
    p.majorKingdomShare = f.numberIn("majorKingdomShare", 0.0, 1.0e6);
    p.minorStateShare = f.numberIn("minorStateShare", 0.0, 1.0e6);
    p.baseTerrainModel = f.text("baseTerrainModel");
    const TerrainModel* model = config.model(p.baseTerrainModel);
    if (model == nullptr) {
        f.fail(f.member("baseTerrainModel"), "baseTerrainModel",
               "unknown terrain model '" + p.baseTerrainModel + "'");
    }
    p.preferences = readPreferences(f, model->preferences);
    readSuitability(f, p);
    readEnergies(f, p);
    readUnderground(f, p);
    p.undergroundPreferences = readWeights(f, "undergroundPreferences", -1.0, 1.0, &kUnderground);
    p.needs = readWeights<1>(f, "needs", 0.0, 1.0, nullptr);
    p.fears = readWeights(f, "fears", 0.0, 1.0, &kFears);
    p.government = f.strings("government");
    if (p.government.empty()) {
        f.fail(f.has("government") ? f.member("government") : document, "government",
               "at least one government");
    }
    p.relations = readWeights<1>(f, "relations", -1.0, 1.0, nullptr);
    p.selfRelation = f.numberIn("selfRelation", -1.0, 1.0, 1.0);
    readRulers(f, p);
    p.document = document;
    return p;
}

void checkPeoples(const std::vector<People>& peoples, const std::vector<std::string>& sources,
                  const std::vector<std::string>& resourceIds) {
    const auto known = [&](const std::string& id) {
        return std::any_of(peoples.begin(), peoples.end(),
                           [&](const People& p) { return p.id == id; });
    };
    // Each id once, before the relations: a duplicate is named as such.
    for (std::size_t i = 0; i < peoples.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (peoples[j].id == peoples[i].id) {
                throw json::Error(sources[i] + ": id: duplicate people '" + peoples[i].id +
                                  "' (also in " + sources[j] + ")");
            }
        }
    }
    for (std::size_t i = 0; i < peoples.size(); ++i) {
        const People& p = peoples[i];
        for (const auto& [other, value] : p.relations) {
            if (other == p.id) {
                throw json::Error(sources[i] + ": relations." + other +
                                  ": a people's opinion of itself is selfRelation");
            }
            if (!known(other)) {
                throw json::Error(sources[i] + ": relations." + other + ": unknown people");
            }
        }
        for (const auto& [need, value] : p.needs) {
            const bool resource =
                std::find(resourceIds.begin(), resourceIds.end(), need) != resourceIds.end();
            if (!resource && !among(need, kNeeds)) {
                throw json::Error(sources[i] + ": needs." + need +
                                  ": unknown need (a resource id, or freshwater, farmland, food, "
                                  "mana, prana)");
            }
        }
    }
}

} // namespace terrain::settlement
