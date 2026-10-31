#pragma once

// The report of terrain_export_settlement: printed as it goes and kept for settlement.txt.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/settlement/borders.hpp"
#include "terrain/generation/settlement/expansion.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/generation/settlement/suitability.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace settlement_tool {

#if defined(__GNUC__)
#define SETTLEMENT_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define SETTLEMENT_PRINTF(f, a)
#endif

class Report {
public:
    // printf of format and the arguments (checked by GCC and Clang).
    void add(const char* format, ...) SETTLEMENT_PRINTF(2, 3);
    [[nodiscard]] const std::string& text() const { return text_; }

private:
    std::string text_;
};

[[nodiscard]] double mebibytes(std::size_t bytes);

// The counts of the geology and of the magic, the statistics of every map, the share of the
// telluric lines and nodes of the world's own network on each terrain model.
void reportMaps(const terrain::settlement::SettlementMaps& m,
                const terrain::settlement::SettlementData& data, Report& r);

// The roughness of about 2,000 land cells against the spread of the real heights of 6 x 6 points
// in each (Spearman).
void reportRoughness(const terrain::Heightfield& field,
                     const terrain::settlement::SettlementMaps& m, Report& r);

// The networks of seeds seed .. seed + count - 1 over the terrain models of the maps: the share
// of their land nodes and of their line points on each model against its share of the land, and
// the models of at least 5 % of the land whose nodes or lines stray by more than 3 points.
void reportEquitySeeds(const terrain::settlement::SettlementMaps& m,
                       const terrain::settlement::SettlementData& data, std::uint64_t seed,
                       int count, Report& r);

// The time and the memory of raceSuitability, and for each people its mean and the share of the
// land where it is at least 0.5.
void reportSuitability(const terrain::settlement::SuitabilityMaps& s,
                       const terrain::settlement::SettlementData& data, Report& r);

// The terrain of the expansion: its time, its memory, the share of the land under rivers (by
// class), lakes, barrier models, the ridge edges, the sea cells of the straits.
void reportTerrain(const terrain::settlement::ExpansionTerrain& t, double seconds, Report& r);

// The ridge edges, and the share of the massifs of mountains touching one.
void reportRidges(const terrain::settlement::ExpansionTerrain& t, Report& r);

// The cuts of the climate found on the rainfall, their latitudes and jumps.
void reportCuts(const std::vector<terrain::settlement::ClimateCut>& cuts, Report& r);

} // namespace settlement_tool
