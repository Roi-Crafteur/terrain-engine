#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_build.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/planet/planet_wind.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

// The planet level at a point of the world (PlanetLevel::sampleAt).
struct PlanetSample {
    // Bilinear between the 4 nearest cell centers.
    float elevation = 0.0f;     // meters
    float precipitation = 0.0f; // mm/yr
    float temperature = 0.0f;   // mean annual, degrees Celsius
    float pet = 0.0f;           // potential evapotranspiration, mm/yr
    float runoff = 0.0f;        // mm/yr
    float waterSurface = 0.0f;  // the sea or lake surface where there is one, else the ground
    // From the cell holding the point.
    bool ocean = false;
    bool frozen = false;
    std::int32_t lake = -1;    // index in lakes(), or -1
    float discharge = 0.0f;    // m^3/s of a river cell, else 0
    std::uint8_t strahler = 0; // of a river cell, else 0
    planet::PlanetWind wind;   // direction of the prevailing wind
};

// The cells of the planet level as kept after the construction, compact (about 5 bytes per
// cell plus the sparse lakes and rivers): what the game needs at run time and 11B will use.
struct PlanetCells {
    PlanetGrid grid;
    std::vector<float> rowTemperature;        // sea-level temperature of each row
    std::vector<std::int16_t> elevation;      // meters, rounded
    std::vector<std::uint16_t> precipitation; // mm/yr, rounded, at most 65,535
    // Per cell: the receiver direction in bits 0-3 (kNoNeighbor for none), the ocean (or the
    // forced outlet) in bit 4, under a lake in bit 5, a river cell in bit 6.
    std::vector<std::uint8_t> bits;
    struct LakeCell {
        std::uint32_t cell;
        std::int32_t lake;
    };
    std::vector<LakeCell> lakeCells; // sorted by cell
    struct RiverCell {
        std::uint32_t cell;
        std::int32_t segment;
        std::int32_t vertex;
    };
    std::vector<RiverCell> riverCells; // sorted by cell: the point of the segment owning it

    static constexpr std::uint8_t kOceanBit = 1u << 4;
    static constexpr std::uint8_t kLakeBit = 1u << 5;
    static constexpr std::uint8_t kRiverBit = 1u << 6;

    [[nodiscard]] std::uint8_t receiver(std::size_t cell) const { return bits[cell] & 0x0Fu; }
    [[nodiscard]] bool ocean(std::size_t cell) const { return (bits[cell] & kOceanBit) != 0; }
    [[nodiscard]] std::int32_t lakeAt(std::size_t cell) const;
    [[nodiscard]] const RiverCell* riverAt(std::size_t cell) const;
    [[nodiscard]] std::size_t memoryBytes() const;
};

// The planet level of a world, computed once from the seed (a few minutes for the whole planet
// on a modest PC, then kept compact): continental climate, lakes, river graph and the places
// 11B carves. Deterministic to the bit on every platform, whatever the number of threads.
class PlanetLevel {
public:
    // Builds the level of the relief of seed and heightfield on world (planet::buildPlanet),
    // then keeps it compact. probe, if set, hears of each stage, then of "level".
    PlanetLevel(std::uint64_t seed, const HeightfieldParams& heightfield,
                const PlanetParams& params = {}, const WorldSize& world = {},
                const PlanetStageProbe& probe = {});
    // Keeps a build compact (build is consumed).
    PlanetLevel(PlanetBuild&& build, const PlanetParams& params);
    // A level kept before (the planet file, storage/planet_file.hpp): everything it keeps.
    struct Parts {
        PlanetParams params;
        PlanetCells cells;
        std::vector<PlanetLake> lakes;
        RiverGraph rivers;
        std::vector<RiverFeature> features;
        PlanetWaterBudget budget;
        PlanetTimings timings;
        PlanetBreach breach;
    };
    explicit PlanetLevel(Parts&& parts);

    [[nodiscard]] const PlanetGrid& grid() const { return cells_.grid; }
    [[nodiscard]] const PlanetCells& cells() const { return cells_; }
    [[nodiscard]] const PlanetParams& params() const { return params_; }

    // Bilinear on the torus for the continuous fields, the cell holding (x, z) for the others.
    [[nodiscard]] PlanetSample sampleAt(double x, double z) const;
    // The humidity of the climate at (x, z), in [0, 1): the precipitation of each cell over its
    // precipitation and potential evapotranspiration, P / (P + PET) (0.5 where they balance, PET
    // read as 300 mm/yr at least), bilinear as sampleAt. What the biomes read of the planet
    // (HumiditySource).
    [[nodiscard]] float humidityAt(double x, double z) const;

    // The climate of a cell, recomputed from its compact altitude and precipitation.
    [[nodiscard]] float elevation(std::size_t cell) const { return cells_.elevation[cell]; }
    [[nodiscard]] float precipitation(std::size_t cell) const { return cells_.precipitation[cell]; }
    [[nodiscard]] float temperature(std::size_t cell) const;
    [[nodiscard]] planet::CellWater water(std::size_t cell) const;

    [[nodiscard]] const std::vector<PlanetLake>& lakes() const { return lakes_; }
    [[nodiscard]] const RiverGraph& rivers() const { return rivers_; }
    [[nodiscard]] const std::vector<RiverFeature>& features() const { return features_; }
    [[nodiscard]] const PlanetWaterBudget& budget() const { return budget_; }
    [[nodiscard]] const PlanetTimings& timings() const { return timings_; }
    [[nodiscard]] const PlanetBreach& breach() const { return breach_; }

    // Drainage basin of every cell (planet::computeBasins), recomputed on demand.
    [[nodiscard]] std::vector<std::int32_t> basins() const;

    // 64-bit FNV-1a checksum of everything kept, over the raw bits, in a fixed order.
    [[nodiscard]] std::uint64_t checksum() const;
    // Bytes held once built: the compact cells, the lakes, the rivers and the features.
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    PlanetParams params_;
    PlanetCells cells_;
    std::vector<PlanetLake> lakes_;
    RiverGraph rivers_;
    std::vector<RiverFeature> features_;
    PlanetWaterBudget budget_;
    PlanetTimings timings_;
    PlanetBreach breach_;
};

namespace planet {

// The compact cells of a build.
[[nodiscard]] PlanetCells compactCells(const PlanetBuild& build);

} // namespace planet

} // namespace terrain
