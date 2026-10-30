#include "settlement_kingdoms.hpp"

#include "terrain/generation/settlement/kingdoms.hpp"
#include "terrain/io/planet_png.hpp"
#include "terrain/io/settlement_png.hpp"

#include <cstdio>
#include <fstream>
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
    const int scale = terrain::planetImageScale(level.grid(), run.width);
    return writeCapitals(run, maps, data, k) &&
           terrain::writePng(
               run.directory / ("kingdoms_" + run.preset + ".png"),
               terrain::renderKingdoms(level, terrain, k, data.config.borders, scale));
}

} // namespace settlement_tool
