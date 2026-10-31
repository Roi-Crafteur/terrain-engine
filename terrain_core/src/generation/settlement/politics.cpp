#include "terrain/generation/settlement/politics.hpp"

#include "settlement_random.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr std::uint64_t kNameAttempts = 8;

// What the land of each state holds and touches, read once.
struct StateLand {
    double cells = 0.0;
    double mana = 0.0;                 // the sum of the bytes of the Mana
    std::vector<double> resources;     // the sums of the bytes of each resource
    std::size_t borderEdges = 0;       // land border edges toward any other owner
    std::vector<double> wildModels;    // by terrain model: the suitability of the wild across
};

struct Survey {
    std::vector<StateLand> states;
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> shared; // (s, t), s < t: edges
};

Survey survey(const ExpansionTerrain& t, const SettlementMaps& maps,
              const SuitabilityMaps& suitability, const SettlementData& data,
              const std::vector<StatePlan>& states, const std::vector<std::uint16_t>& owner) {
    Survey v;
    StateLand empty;
    empty.resources.assign(data.resources.size(), 0.0);
    empty.wildModels.assign(data.config.terrainModels.size(), 0.0);
    v.states.assign(states.size(), empty);
    const PlanetGrid& g = t.grid;
    t.land->forEachLand([&](std::size_t cell, std::size_t a) {
        if (owner[a] != 0) {
            StateLand& s = v.states[owner[a] - 1u];
            s.cells += 1.0;
            s.mana += maps.mana.values()[a];
            for (std::size_t r = 0; r < s.resources.size(); ++r) {
                s.resources[r] += maps.resources[r].values()[a];
            }
        }
        for (const bool east : {true, false}) {
            const std::size_t n = east ? g.index(g.wrapColumn(g.column(cell) + 1), g.row(cell))
                                       : g.index(g.column(cell), g.wrapRow(g.row(cell) + 1));
            if (!t.land->land(n) || owner[a] == owner[t.land->slot(n)]) {
                continue;
            }
            const std::size_t b = t.land->slot(n);
            for (const auto& [mine, other] : {std::pair{a, b}, std::pair{b, a}}) {
                if (owner[mine] == 0) {
                    continue;
                }
                const std::size_t s = owner[mine] - 1u;
                ++v.states[s].borderEdges;
                if (owner[other] == 0) {
                    const auto people = static_cast<std::size_t>(states[s].people);
                    v.states[s].wildModels[maps.terrainModel.values()[other]] +=
                        unquantize(suitability.byPeople[people].values()[other]);
                }
            }
            if (owner[a] != 0 && owner[b] != 0) {
                const std::size_t s = owner[a] - 1u;
                const std::size_t u = owner[b] - 1u;
                ++v.shared[{std::min(s, u), std::max(s, u)}];
            }
        }
    });
    return v;
}

std::string roman(int n) {
    constexpr const char* kNumerals[] = {"I",   "II",   "III", "IV", "V",    "VI",  "VII",
                                         "VIII", "IX",  "X",   "XI", "XII", "XIII", "XIV",
                                         "XV",  "XVI", "XVII", "XVIII", "XIX", "XX"};
    return kNumerals[std::clamp(n, 1, 20) - 1];
}

// count syllables of a people drawn from the stream at index, joined by joint.
std::string syllables(const People& p, std::uint64_t stream, std::uint64_t index, int count,
                      const char* joint) {
    std::string out;
    for (int k = 0; k < count; ++k) {
        const int pick = drawBetween(stream, index + static_cast<std::uint64_t>(k), 0,
                                     static_cast<int>(p.syllables.size()) - 1);
        out += (k > 0 ? joint : "") + p.syllables[static_cast<std::size_t>(pick)];
    }
    return out;
}

std::string capitalized(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') {
        s[0] = static_cast<char>(s[0] - 'a' + 'A');
    }
    return s;
}

// The draws of the names of state s: the first from the stream "ruler" (attempt 0), the next ones,
// when the name is already taken, from the stream "rulerRetry".
struct NameDraws {
    std::uint64_t first = 0;
    std::uint64_t retry = 0;
    std::uint64_t base = 0; // of state s in "ruler"
    std::size_t s = 0;

    // The stream and the first index of the draws of an attempt; the offsets of "ruler" for the
    // attempt 0 (count, kingdom, first name, regnal) and of "rulerRetry" for the others.
    std::uint64_t stream(std::uint64_t attempt) const { return attempt == 0 ? first : retry; }
    std::uint64_t index(std::uint64_t attempt, std::uint64_t offset,
                        std::uint64_t retryOffset) const {
        return attempt == 0 ? base + offset
                            : (static_cast<std::uint64_t>(s) * kNameAttempts + attempt) * 32u +
                                  retryOffset;
    }
};

// The first name drawn by draw(attempt) that is not in taken, at most kNameAttempts draws (the
// last one is kept even if taken).
template <typename Draw>
std::string freeName(const std::set<std::string>& taken, Draw draw) {
    std::string name = draw(0);
    for (std::uint64_t attempt = 1; attempt < kNameAttempts && taken.count(name) != 0; ++attempt) {
        name = draw(attempt);
    }
    return name;
}

// The ruler of state s, without its goals: its government, names and personality. Its kingdom
// gets a name no other kingdom has (taken in kingdoms), redrawn if needed, else followed by the
// number of the state; its own name is redrawn as long as another ruler has it (names).
Ruler rulerOf(const SettlementData& data, const StatePlan& state, std::size_t s,
              std::uint64_t worldSeed, std::set<std::string>& kingdoms,
              std::set<std::string>& names) {
    const People& p = data.peoples[static_cast<std::size_t>(state.people)];
    const PoliticsRule& rule = data.config.politics;
    const NameDraws d{streamOf(worldSeed, "ruler"), streamOf(worldSeed, "rulerRetry"),
                      static_cast<std::uint64_t>(s) * 64u, s};
    Ruler r;
    char id[32];
    std::snprintf(id, sizeof id, "ruler_%03zu", s);
    r.id = id;
    r.government = p.government[static_cast<std::size_t>(
        drawBetween(d.first, d.base, 0, static_cast<int>(p.government.size()) - 1))];
    for (std::size_t k = 0; k < r.personality.size(); ++k) {
        const TraitRange& range = p.traits[k];
        r.personality[k] = drawIn(d.first, d.base + 1 + k, RealRange{range.min, range.max});
    }
    r.kingdom = freeName(kingdoms, [&](std::uint64_t a) {
        const int count = drawBetween(d.stream(a), d.index(a, 8, 0), rule.kingdomSyllablesMin,
                                      rule.kingdomSyllablesMax);
        return syllables(p, d.stream(a), d.index(a, 16, 1), count, "_");
    });
    if (kingdoms.count(r.kingdom) != 0) {
        r.kingdom += "_" + std::to_string(s);
    }
    kingdoms.insert(r.kingdom);
    const std::string title = p.titleOf(r.government);
    r.name = freeName(names, [&](std::uint64_t a) {
        const std::string first =
            capitalized(syllables(p, d.stream(a), d.index(a, 32, 16), rule.nameSyllables, ""));
        return (title.empty() ? "" : title + " ") + first + " " +
               roman(drawBetween(d.stream(a), d.index(a, 48, 24), 1, rule.regnalMax));
    });
    names.insert(r.name);
    return r;
}

double mean(double sum, double cells) {
    return cells > 0.0 ? sum / cells / 255.0 : 0.0;
}

// The resources a people needs (at least neededWeight), as indices of data.resources and weights.
std::vector<std::pair<std::size_t, double>> neededResources(const SettlementData& data,
                                                            const People& p, double least) {
    std::vector<std::pair<std::size_t, double>> out;
    for (const auto& [need, weight] : p.needs) {
        for (std::size_t r = 0; r < data.resources.size(); ++r) {
            if (data.resources[r].id == need && weight >= least) {
                out.emplace_back(r, weight);
            }
        }
    }
    return out;
}

StateRelation relationOf(const SettlementData& data, const std::vector<StatePlan>& states,
                         const Survey& v, const std::vector<Ruler>& rulers, std::size_t s,
                         std::size_t u, std::size_t edges) {
    const People& a = data.peoples[static_cast<std::size_t>(states[s].people)];
    const People& b = data.peoples[static_cast<std::size_t>(states[u].people)];
    const TensionWeights& w = data.config.politics.tension;
    StateRelation r;
    r.from = s;
    r.to = u;
    r.borderEdges = edges;
    r.relation = a.relationTo(b.id); // selfRelation for one people
    r.border = v.states[s].borderEdges > 0
                   ? static_cast<double>(edges) / static_cast<double>(v.states[s].borderEdges)
                   : 0.0;
    double weights = 0.0;
    for (const auto& [res, weight] : neededResources(data, a, 0.0)) {
        const double mine = mean(v.states[s].resources[res], v.states[s].cells);
        const double theirs = mean(v.states[u].resources[res], v.states[u].cells);
        r.coveted += weight * std::max(0.0, theirs - mine);
        weights += weight;
    }
    r.coveted = weights > 0.0 ? std::min(1.0, r.coveted / weights) : 0.0;
    const double pressure = std::clamp(1.0 - v.states[s].cells / states[s].targetCells, 0.0, 1.0);
    r.tension = std::clamp(w.border * r.border + w.resources * r.coveted +
                               w.ambition * rulers[s].personality[0] + w.pressure * pressure,
                           0.0, 1.0);
    return r;
}

// The goals of the ruler of state s, the best first, at most rule.maxGoals, at least one.
std::vector<std::string> goalsOf(const SettlementData& data, const std::vector<StatePlan>& states,
                                 const Survey& v, const std::vector<Ruler>& rulers,
                                 const std::vector<StateRelation>& relations, std::size_t s) {
    const PoliticsRule& rule = data.config.politics;
    const People& p = data.peoples[static_cast<std::size_t>(states[s].people)];
    const StateLand& land = v.states[s];
    const auto& trait = rulers[s].personality; // ambition, aggression, greed, prudence, piety
    std::vector<std::pair<double, std::string>> goals;
    for (const auto& [res, weight] : neededResources(data, p, rule.neededWeight)) {
        const double held = mean(land.resources[res], land.cells);
        if (held < rule.scarceResource) {
            goals.emplace_back(weight * (1.0 - held / rule.scarceResource) * (0.5 + trait[2]),
                               "secure_" + data.resources[res].id);
        }
    }
    const auto best = std::max_element(land.wildModels.begin(), land.wildModels.end());
    if (trait[0] >= rule.expandAmbition && best != land.wildModels.end() && *best > 0.0) {
        goals.emplace_back(trait[0], "expand_into_" +
                                         data.config.terrainModels[static_cast<std::size_t>(
                                             best - land.wildModels.begin())].id);
    }
    const StateRelation* rival = nullptr;
    for (const StateRelation& r : relations) {
        if (r.from == s && (rival == nullptr || r.tension > rival->tension)) {
            rival = &r;
        }
    }
    if (rival != nullptr && trait[1] >= rule.rivalAggression &&
        rival->tension >= rule.rivalTension) {
        goals.emplace_back(trait[1] * rival->tension, "weaken_" + rulers[rival->to].kingdom);
    }
    if (p.mana >= rule.manaPreference && mean(land.mana, land.cells) < rule.scarceMana) {
        goals.emplace_back(p.mana, "seek_mana");
    }
    if (trait[3] >= rule.defendPrudence) {
        goals.emplace_back(trait[3], "defend_borders");
    }
    std::stable_sort(goals.begin(), goals.end(),
                     [](const auto& x, const auto& y) { return x.first > y.first; });
    std::vector<std::string> out;
    for (const auto& goal : goals) {
        if (static_cast<int>(out.size()) < rule.maxGoals) {
            out.push_back(goal.second);
        }
    }
    if (out.empty()) {
        out.push_back("defend_borders");
    }
    return out;
}

} // namespace

Politics buildPolitics(const ExpansionTerrain& terrain, const SettlementMaps& maps,
                       const SuitabilityMaps& suitability, const SettlementData& data,
                       const std::vector<StatePlan>& states,
                       const std::vector<std::uint16_t>& owner, std::uint64_t worldSeed) {
    const Survey v = survey(terrain, maps, suitability, data, states, owner);
    Politics out;
    std::set<std::string> kingdoms;
    std::set<std::string> names;
    for (std::size_t s = 0; s < states.size(); ++s) {
        out.rulers.push_back(rulerOf(data, states[s], s, worldSeed, kingdoms, names));
    }
    for (const auto& [pair, edges] : v.shared) {
        const auto [s, u] = pair;
        out.relations.push_back(relationOf(data, states, v, out.rulers, s, u, edges));
        out.relations.push_back(relationOf(data, states, v, out.rulers, u, s, edges));
    }
    std::sort(out.relations.begin(), out.relations.end(), [](const auto& a, const auto& b) {
        return a.from != b.from ? a.from < b.from : a.to < b.to;
    });
    for (std::size_t s = 0; s < states.size(); ++s) {
        out.rulers[s].goals = goalsOf(data, states, v, out.rulers, out.relations, s);
    }
    return out;
}

} // namespace terrain::settlement
