#include "settlement_kingdoms.hpp"

#include "terrain/generation/settlement/capitals.hpp"
#include "terrain/generation/settlement/geology.hpp"
#include "terrain/generation/settlement/world_preset.hpp"

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

void reportStates(const st::SettlementData& data, const std::vector<st::StateCount>& counts,
                  Report& r) {
    std::string line = "  states by people:";
    for (std::size_t p = 0; p < counts.size(); ++p) {
        char part[64];
        std::snprintf(part, sizeof part, " %s %d+%d", data.peoples[p].id.c_str(), counts[p].major,
                      counts[p].minor);
        line += part;
    }
    r.add("%s\n", line.c_str());
}

void reportCapitals(const st::CapitalChoice& c, Report& r) {
    int fails = 0;
    int relaxed = 0;
    int fallbacks = 0;
    for (const st::Capital& k : c.capitals) {
        fails += k.topHabitability && k.nearFreshWater ? 0 : 1;
        relaxed += k.relaxations > 0 ? 1 : 0;
        fallbacks += k.seed ? 0 : 1;
    }
    r.add("  capitals in %.1f s: %zu candidates (habitability at least %d/255 and fresh water near), "
          "%zu seeds; %d relaxed, %d not on a seed, %d outside the most habitable fifth or far "
          "from fresh water\n",
          c.seconds, c.candidates, static_cast<int>(c.habitabilityThreshold), c.seeds, relaxed,
          fallbacks, fails);
}

// capitals_<preset>.txt: one line per state, in the coordinates of the engine.
bool writeCapitals(const PresetRun& run, const st::SettlementMaps& maps,
                   const st::SettlementData& data, const st::ResolvedPreset& preset,
                   const std::vector<st::StatePlan>& plans, const st::CapitalChoice& c) {
    std::ofstream out(run.directory / ("capitals_" + run.preset + ".txt"),
                      std::ios::binary | std::ios::trunc);
    char line[512];
    std::snprintf(line, sizeof line,
                  "# capitals of the preset %s: seed %llu, %d major kingdoms, %d minor states, "
                  "civilized share %.3f\n# state people rank x_m z_m target_km2 habitability "
                  "fresh_water_km on_seed relaxations\n",
                  preset.preset.c_str(), static_cast<unsigned long long>(run.seed),
                  preset.majorKingdoms, preset.minorStates, preset.civilizedShare);
    out << line;
    const double cellKm2 = maps.grid.cellArea() * 1e-6;
    for (std::size_t s = 0; s < plans.size(); ++s) {
        const st::Capital& k = c.capitals[s];
        std::snprintf(line, sizeof line, "%zu %s %s %.0f %.0f %.0f %.3f %d %s %d\n", s,
                      data.peoples[static_cast<std::size_t>(plans[s].people)].id.c_str(),
                      rankName(plans[s].rank), k.x, k.z, plans[s].targetCells * cellKm2,
                      st::unquantize(maps.habitability[k.cell]), maps.waterKm[k.cell],
                      k.seed ? "yes" : "no", k.relaxations);
        out << line;
    }
    return static_cast<bool>(out);
}

} // namespace

bool runPreset(const st::SettlementMaps& maps, const st::SuitabilityMaps& suitability,
               const st::SettlementData& data, const PresetRun& run, Report& r) {
    const double scale = st::worldScale(maps.grid, data.config);
    const st::ResolvedPreset preset = st::resolvePreset(data.config, {run.preset, {}, {}, {}},
                                                        run.seed, scale);
    const std::vector<st::StateCount> counts =
        st::allocateStates(data.peoples, preset.majorKingdoms, preset.minorStates);
    const std::vector<st::StatePlan> plans = st::planStates(
        data.config, preset, data.peoples, counts, maps.land->landCells(), run.seed);
    r.add("preset %s: civilized share %.3f, %d major kingdoms, %d minor states drawn, %zu states\n",
          preset.preset.c_str(), preset.civilizedShare, preset.majorKingdoms, preset.minorStates,
          plans.size());
    reportStates(data, counts, r);
    const st::CapitalChoice capitals =
        st::placeCapitals(maps, suitability, data.config.capitals, plans, run.threads);
    reportCapitals(capitals, r);
    return writeCapitals(run, maps, data, preset, plans, capitals);
}

} // namespace settlement_tool
