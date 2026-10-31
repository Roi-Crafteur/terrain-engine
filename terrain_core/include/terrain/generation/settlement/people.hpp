#pragma once

#include "terrain/generation/settlement/preference.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"
#include "terrain/json/json.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A people of the settlement (docs/PEUPLEMENT.md, sections 5 and 6): a data file, one per people,
// read and validated by the core; adding a people is adding a file. Its physical preferences
// start from its base terrain model and are adjusted criterion by criterion.
namespace terrain::settlement {

enum class Underground : std::uint8_t { None, Partial, Mostly, Full };

// The five traits of a ruler, in this order, each drawn in a range of the people.
inline constexpr std::array<std::string_view, 5> kRulerTraits = {"ambition", "aggression", "greed",
                                                                 "prudence", "piety"};

struct TraitRange {
    double min = 0.0;
    double max = 1.0;
};

using Weights = std::vector<std::pair<std::string, double>>;

struct People {
    std::string id;
    std::string name;
    std::string plane = "material";
    double populationShare = 0.0; // read, without effect in V1 (no population yet)
    // Relative shares of the major kingdoms and of the minor states (normalized over the peoples).
    double majorKingdomShare = 0.0;
    double minorStateShare = 0.0;
    std::string baseTerrainModel;
    Preferences preferences; // the base model's, adjusted by the file
    // The weights of raceSuitability (section 6).
    double habitabilityWeight = 1.0;
    double physicalWeight = 1.0;
    double dangerSensitivity = 0.5;
    double prana = 0.0; // ambientEnergyPreferences, in [-1, 1]
    double aura = 0.0;  // read, without effect in V1 (no Aura map)
    double mana = 0.0;
    // Read and kept, without effect in V1 (the underground life comes later).
    Underground underground = Underground::None;
    double preferredDepth = 0.0;
    bool needsSurfaceAccess = true;
    Weights undergroundPreferences;
    // What it needs (resource ids, or freshwater, farmland, food, mana, prana) and what it fears
    // (highMagic, volcano, deepOcean, monsterDensity), in [0, 1].
    Weights needs;
    Weights fears;
    std::vector<std::string> government; // at least one
    Weights relations;                   // toward other peoples, [-1, 1]; absent: 0
    double selfRelation = 1.0;
    std::array<TraitRange, 5> traits{};
    std::vector<std::string> syllables; // for the names of its rulers
    std::vector<std::pair<std::string, std::string>> titles; // government -> title
    json::Value document;

    // Its opinion of another people: selfRelation for itself, 0 when its file says nothing.
    [[nodiscard]] double relationTo(std::string_view other) const;
    // The title of a ruler of a kind of government ("" if none).
    [[nodiscard]] std::string titleOf(std::string_view kind) const;
};

// A people of a document; its base terrain model must be one of config. Throws json::Error with
// the source, the path and the reason.
[[nodiscard]] People readPeople(const json::Value& document, std::string_view source,
                                const SettlementConfig& config);

// The checks across the peoples of a world: each id once, relations toward known peoples (and not
// toward itself: selfRelation), needs among the resource ids and the known needs. sources[k] names
// the file of peoples[k]. Throws json::Error.
void checkPeoples(const std::vector<People>& peoples, const std::vector<std::string>& sources,
                  const std::vector<std::string>& resourceIds);

} // namespace terrain::settlement
