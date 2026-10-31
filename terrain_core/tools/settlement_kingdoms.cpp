#include "settlement_kingdoms.hpp"

#include "terrain/generation/settlement/kingdoms.hpp"
#include "terrain/generation/settlement/people.hpp"
#include "terrain/json/json.hpp"
#include "terrain/io/planet_png.hpp"
#include "terrain/io/settlement_png.hpp"
#include "terrain/storage/settlement_file.hpp"
#include "terrain/storage/storage_error.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace st = terrain::settlement;

namespace settlement_tool {

namespace {

const char* rankName(st::StateRank rank) {
    return rank == st::StateRank::Major ? "major" : "minor";
}

double share(std::size_t part, std::size_t whole) {
    return whole > 0 ? static_cast<double>(part) / static_cast<double>(whole) : 0.0;
}

void reportStates(const st::SettlementData& data, const st::Kingdoms& k, Report& r) {
    std::string line = "  states by people (major+minor):";
    for (std::size_t p = 0; p < k.counts.size(); ++p) {
        char part[64];
        std::snprintf(part, sizeof part, " %s %d+%d", data.peoples[p].id.c_str(),
                      k.counts[p].major, k.counts[p].minor);
        line += part;
    }
    r.add("%s\n", line.c_str());
    const st::CapitalChoice& c = k.capitals;
    int relaxed = 0;
    int fallbacks = 0;
    int minors = 0;
    int fitted = 0;
    for (std::size_t s = 0; s < c.capitals.size(); ++s) {
        relaxed += c.capitals[s].relaxations > 0 ? 1 : 0;
        fallbacks += c.capitals[s].seed ? 0 : 1;
        minors += k.states[s].rank == st::StateRank::Minor ? 1 : 0;
        fitted += c.capitals[s].compartment ? 1 : 0;
    }
    r.add("  capitals: %zu candidates (habitability at least %d/255, fresh water at most 10 km), "
          "%zu seeds; %d relaxed, %d not on a seed; %d of the %d minor states in a compartment of "
          "their size\n",
          c.candidates, static_cast<int>(c.habitabilityThreshold), c.seeds, relaxed, fallbacks,
          fitted, minors);
}

void reportGoodWorld(const st::SettlementData& data, const st::Kingdoms& k, Report& r) {
    const st::KingdomStats& s = k.stats;
    const st::BorderStats& b = s.borders;
    const auto verdict = [](bool ok) { return ok ? "ok" : "FAILS"; };
    r.add("  good world: peoples without land %d [%s]; largest state %.2f %% of the land [%s]; "
          "largest people %s %.1f %% of the land held [%s]\n",
          s.peoplesWithoutState, verdict(s.peoplesWithoutState == 0), 100.0 * s.largestState,
          verdict(s.largestState <= 0.15),
          s.largestPeopleIndex >= 0
              ? data.peoples[static_cast<std::size_t>(s.largestPeopleIndex)].id.c_str()
              : "-",
          100.0 * s.largestPeople, verdict(s.largestPeople <= 0.40));
    r.add("  land borders: %zu edges, natural %.1f %% [%s] (river or lake %.1f %%, ridge %.1f %%, "
          "coast %.1f %%, hostile land %.1f %%); between two states %zu edges, natural %.1f %%; of "
          "all the land edges %.1f %% natural\n",
          b.edges, 100.0 * b.naturalShare(), verdict(b.naturalShare() >= 0.5),
          100.0 * share(b.river, b.edges), 100.0 * share(b.ridge, b.edges),
          100.0 * share(b.coast, b.edges), 100.0 * share(b.hostile, b.edges), b.betweenStates,
          100.0 * b.naturalShareBetweenStates(),
          100.0 * share(b.landEdgesNatural, b.landEdges));
    r.add("  capitals outside the most habitable fifth or far from fresh water: %d [%s]; hostile "
          "land (%zu cells): %.1f %% held, %.1f %% by peoples that do not like it\n",
          s.capitalFails, verdict(s.capitalFails == 0), s.hostileCells, 100.0 * s.hostileHeld,
          100.0 * s.hostileUnliked);
    const st::MassifStats& m = s.massifs;
    r.add("  mountains: %zu massifs of at least 25 cells (%zu cells): %.1f %% of their land held; "
          "%zu massifs (%.1f %% of their land) held by two states or more, %zu of them (%.1f %%) "
          "with two states meeting on a ridge; borders between two states in the massifs: %zu "
          "edges, %.1f %% on a ridge; borders along the foot of a massif held by no one: %.1f %% "
          "of the land borders, %.1f %% of them natural\n",
          m.massifs, m.cells, 100.0 * share(m.held, m.cells), m.shared,
          100.0 * share(m.sharedCells, m.cells), m.ridgeMeet,
          100.0 * share(m.ridgeMeetCells, m.cells), m.statesEdges,
          100.0 * share(m.statesRidgeEdges, m.statesEdges), 100.0 * share(m.footEdges, b.edges),
          100.0 * share(m.footNatural, m.footEdges));
    r.add("  shapes: mean elongation (1 round, 2 twice as long as wide) of the major kingdoms "
          "%.3f, of the minor states %.3f\n",
          s.majorElongation, s.minorElongation);
    r.add("  climate cuts: %.2f %% of the border edges within a cell of a cut (%.2f %% of all the "
          "land edges), %zu border edges on a cut\n",
          100.0 * share(b.nearCuts, b.edges), 100.0 * share(b.landEdgesNearCuts, b.landEdges),
          b.alongCuts);
    r.add("  held %.1f %% of the land; %d reruns; seconds: capitals %.1f, expansion %.1f (%zu "
          "entries), cleanup %.1f, analysis %.1f; %.0f MiB; checksum %016llx\n",
          100.0 * s.heldShare, k.reruns, k.timings.capitals, k.timings.expansion, k.pushes,
          k.timings.cleanup, k.timings.analysis, mebibytes(k.memoryBytes()),
          static_cast<unsigned long long>(k.checksum()));
}

namespace json = terrain::json;

// A value rounded to the hundredth, for the files read by people.
double hundredths(double v) {
    return static_cast<double>(static_cast<int>(v * 100.0 + (v < 0.0 ? -0.5 : 0.5))) / 100.0;
}

// rulers_<preset>.json in the format of the document, and relations_<preset>.json: what each
// state thinks of each neighbor.
bool writePolitics(const PresetRun& run, const st::Kingdoms& k) {
    json::Value rulers = json::Value::array();
    for (const st::Ruler& r : k.politics.rulers) {
        json::Value v = json::Value::object();
        v.set("id", json::Value::string(r.id));
        v.set("kingdom", json::Value::string(r.kingdom));
        v.set("government", json::Value::string(r.government));
        v.set("name", json::Value::string(r.name));
        json::Value personality = json::Value::object();
        for (std::size_t t = 0; t < r.personality.size(); ++t) {
            personality.set(std::string(st::kRulerTraits[t]),
                            json::Value::number(hundredths(r.personality[t])));
        }
        v.set("personality", std::move(personality));
        json::Value goals = json::Value::array();
        for (const std::string& g : r.goals) {
            goals.push(json::Value::string(g));
        }
        v.set("goals", std::move(goals));
        rulers.push(std::move(v));
    }
    json::Value relations = json::Value::array();
    for (const st::StateRelation& r : k.politics.relations) {
        json::Value v = json::Value::object();
        v.set("from", json::Value::string(k.politics.rulers[r.from].kingdom));
        v.set("to", json::Value::string(k.politics.rulers[r.to].kingdom));
        v.set("relation", json::Value::number(hundredths(r.relation)));
        v.set("tension", json::Value::number(hundredths(r.tension)));
        relations.push(std::move(v));
    }
    std::ofstream a(run.directory / ("rulers_" + run.preset + ".json"), std::ios::binary);
    a << json::write(rulers) << '\n';
    std::ofstream b(run.directory / ("relations_" + run.preset + ".json"), std::ios::binary);
    b << json::write(relations) << '\n';
    return static_cast<bool>(a) && static_cast<bool>(b);
}

void reportPolitics(const st::Kingdoms& k, Report& r) {
    const st::Politics& p = k.politics;
    double tension = 0.0;
    int high = 0;
    int hostile = 0;
    for (const st::StateRelation& rel : p.relations) {
        tension += rel.tension;
        high += rel.tension >= 0.5 ? 1 : 0;
        hostile += rel.relation < 0.0 ? 1 : 0;
    }
    std::map<std::string, int> goals;
    for (const st::Ruler& ruler : p.rulers) {
        for (const std::string& g : ruler.goals) {
            std::string kind = g;
            for (const char* prefix : {"secure_", "expand_into_", "weaken_"}) {
                if (g.rfind(prefix, 0) == 0) {
                    kind = std::string(prefix) + "*";
                }
            }
            ++goals[kind];
        }
    }
    std::string list;
    for (const auto& [goal, count] : goals) {
        list += " " + goal + " " + std::to_string(count) + ";";
    }
    const double n = std::max<double>(1.0, static_cast<double>(p.relations.size()));
    r.add("  rulers %zu, relations %zu (both ways between neighbors): mean tension %.2f, "
          "%.1f %% at 0.5 or more, %.1f %% of hostile peoples; goals:%s\n",
          p.rulers.size(), p.relations.size(), tension / n, 100.0 * high / n, 100.0 * hostile / n,
          list.c_str());
    for (std::size_t s = 0; s < std::min<std::size_t>(2, p.rulers.size()); ++s) {
        const st::Ruler& ruler = p.rulers[s];
        std::string g;
        for (const std::string& goal : ruler.goals) {
            g += " " + goal;
        }
        r.add("    %s of %s (%s):%s\n", ruler.name.c_str(), ruler.kingdom.c_str(),
              ruler.government.c_str(), g.c_str());
    }
}

// capitals_<preset>.txt: one line per state, in the coordinates of the engine.
bool writeCapitals(const PresetRun& run, const st::SettlementMaps& maps,
                   const st::SettlementData& data, const st::Kingdoms& k) {
    std::ofstream out(run.directory / ("capitals_" + run.preset + ".txt"),
                      std::ios::binary | std::ios::trunc);
    char line[512];
    std::snprintf(line, sizeof line,
                  "# capitals of the preset %s: seed %llu, %d major kingdoms, %d minor states, "
                  "civilized share %.3f\n# state people rank x_m z_m area_km2 target_km2 "
                  "habitability fresh_water_km on_seed relaxations\n",
                  k.preset.preset.c_str(), static_cast<unsigned long long>(run.seed),
                  k.preset.majorKingdoms, k.preset.minorStates, k.preset.civilizedShare);
    out << line;
    const double cellKm2 = maps.grid.cellArea() * 1e-6;
    for (std::size_t s = 0; s < k.states.size(); ++s) {
        const st::Capital& c = k.capitals.capitals[s];
        std::snprintf(line, sizeof line, "%zu %s %s %.0f %.0f %.0f %.0f %.3f %d %s %d\n", s,
                      data.peoples[static_cast<std::size_t>(k.states[s].people)].id.c_str(),
                      rankName(k.states[s].rank), c.x, c.z,
                      static_cast<double>(k.cells[s]) * cellKm2, k.states[s].targetCells * cellKm2,
                      st::unquantize(maps.habitability[c.cell]), maps.waterKm[c.cell],
                      c.seed ? "yes" : "no", c.relaxations);
        out << line;
    }
    return static_cast<bool>(out);
}

// settlement_<preset>.json: what the world keeps of its settlement (storage/settlement_file.hpp),
// read back once to check it.
bool writeSettlement(const PresetRun& run, const st::SettlementData& data, const st::Kingdoms& k,
                     Report& r) {
    terrain::SettlementRecord record;
    record.worldSeed = run.seed;
    record.request = {run.preset, {}, {}, {}};
    record.resolved = k.preset;
    record.config = data.document();
    record.configVersion = data.config.version;
    record.kingdomsChecksum = k.checksum();
    record.generationState = {{"terrain", "done"},       {"rivers", "done"},
                              {"coasts", "pending"},     {"civilizations", "done"},
                              {"hiddenLayer", "pending"}};
    const std::filesystem::path file = run.directory / ("settlement_" + run.preset + ".json");
    try {
        terrain::saveSettlementFile(file, record);
        const terrain::SettlementRecord back = terrain::loadSettlementFile(file);
        r.add("  settlement_%s.json: configHash %016llx, kingdoms %016llx\n", run.preset.c_str(),
              static_cast<unsigned long long>(terrain::settlementConfigHash(back.config)),
              static_cast<unsigned long long>(back.kingdomsChecksum));
    } catch (const terrain::StorageError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return false;
    }
    return true;
}

} // namespace

bool runPreset(const terrain::PlanetLevel& level, const st::SettlementMaps& maps,
               const st::SuitabilityMaps& suitability, const st::ExpansionTerrain& terrain,
               const st::SettlementData& data, const PresetRun& run, Report& r) {
    const st::Kingdoms k = st::buildKingdoms(level, maps, suitability, terrain, data,
                                             {run.preset, {}, {}, {}}, run.seed, run.threads);
    r.add("preset %s: civilized share %.3f, %d major kingdoms and %d minor states drawn, %zu "
          "states\n",
          k.preset.preset.c_str(), k.preset.civilizedShare, k.preset.majorKingdoms,
          k.preset.minorStates, k.states.size());
    reportStates(data, k, r);
    reportGoodWorld(data, k, r);
    reportPolitics(k, r);
    const int scale = terrain::planetImageScale(level.grid(), run.width);
    if (run.zoom != nullptr &&
        !terrain::writePng(run.directory / ("kingdoms_" + run.preset + "_zoom.png"),
                           terrain::renderKingdomsZoom(level, terrain, k, data.config.borders,
                                                       *run.zoom))) {
        return false;
    }
    return writeCapitals(run, maps, data, k) && writePolitics(run, k) &&
           writeSettlement(run, data, k, r) &&
           terrain::writePng(
               run.directory / ("kingdoms_" + run.preset + ".png"),
               terrain::renderKingdoms(level, terrain, k, data.config.borders, scale));
}

} // namespace settlement_tool
