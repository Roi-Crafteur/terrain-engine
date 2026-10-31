#pragma once

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/settlement/geology.hpp"
#include "terrain/generation/settlement/preference.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/telluric.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain {
class Heightfield;
class PlanetLevel;
}

// The analysis maps of the settlement on the planet level (docs/PEUPLEMENT.md, sections 4,
// 4 ter, 6 bis): derived data, kept apart from the level (its checksum never changes), computed
// again from it whenever the relief changes. One byte per land cell and per map: a score in
// [0, 1] as 0 to 255, a distance in km (255: farther than kMaxDistanceKm), an index.
namespace terrain::settlement {

// The bits of SettlementMaps::water.
inline constexpr std::uint8_t kRiverClassMask = 0x07; // 0 none, 1 river ... 4 impassable
inline constexpr std::uint8_t kFreshLakeBit = 0x08;   // under a lake that spills, not frozen
inline constexpr std::uint8_t kSaltLakeBit = 0x10;    // under a lake without outlet or a playa
inline constexpr std::uint8_t kClosedBasinBit = 0x20; // drains to no sea
inline constexpr std::uint8_t kFrozenBit = 0x40;      // frozen ground

// Distances are exact (between the centers of the cells) up to this many cells, 255 beyond.
inline constexpr int kMaxDistanceCells = 32;
inline constexpr std::uint8_t kFarDistance = 255;

struct SettlementMapTimings {
    double inputs = 0.0;
    double distances = 0.0;
    double geology = 0.0;
    double network = 0.0;
    double cells = 0.0;

    [[nodiscard]] double total() const { return inputs + distances + geology + network + cells; }
};

struct SettlementMaps {
    PlanetGrid grid;
    std::shared_ptr<const planet::LandIndex> land;
    planet::LandValues<std::uint8_t> water;    // the bits above
    planet::LandValues<std::uint8_t> relief;   // the intensity of the relief of the geography
    planet::LandValues<std::uint8_t> roughness;
    planet::LandValues<std::uint8_t> waterKm;  // to the fresh water
    planet::LandValues<std::uint8_t> coastKm;  // to the sea
    planet::LandValues<std::uint8_t> vegetation;
    planet::LandValues<std::uint8_t> terrainModel; // index in SettlementConfig::terrainModels
    planet::LandValues<std::uint8_t> arable;
    planet::LandValues<std::uint8_t> food;
    planet::LandValues<std::uint8_t> danger;   // natural
    planet::LandValues<std::uint8_t> prana;
    planet::LandValues<std::uint8_t> telluric; // the influence of the telluric lines alone
    planet::LandValues<std::uint8_t> mana;     // usable: on the land
    planet::LandValues<std::uint8_t> habitability;
    std::vector<planet::LandValues<std::uint8_t>> resources; // by SettlementData::resources
    Geology geology;
    TelluricNetwork network;
    SettlementMapTimings timings;

    // The criteria of a land cell: altitude, temperature, rainfall, humidity from the level,
    // roughness and distances from these maps, latitude from its row.
    [[nodiscard]] CriterionValues criteria(const PlanetLevel& level, std::size_t cell) const;
    // The cell holding (x, z), for the queries at a point.
    [[nodiscard]] std::size_t cellAt(double x, double z) const;
    [[nodiscard]] std::size_t memoryBytes() const;
    // FNV-1a 64 of every map and every point, in a fixed order: the same bits whatever the threads.
    [[nodiscard]] std::uint64_t checksum() const;
};

// The maps of a level. field, when not null, is the relief the level was built from (the
// Heightfield of worldSeed): 4 heights in each land cell for its roughness, and its geography for
// the intensity of the relief, the belts and the plates of the geology. Without it (levels built
// by hand) these are 0, the roughness comes from the floors alone and the geology has no fault.
// The same bits whatever threads.
[[nodiscard]] SettlementMaps computeSettlementMaps(const PlanetLevel& level,
                                                   const Heightfield* field,
                                                   const SettlementData& data,
                                                   std::uint64_t worldSeed, int threads);

// The humidity of the climate of a cell, as PlanetLevel::humidityAt reads it at its center.
[[nodiscard]] double cellHumidity(const PlanetLevel& level, std::size_t cell);

} // namespace terrain::settlement
