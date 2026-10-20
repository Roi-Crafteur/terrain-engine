#pragma once

// Golden checksums of the caves (see golden_support.hpp): voids, densities and water of LOD 0
// to 2 at fixed points, aquifer cells, entrances, speleothems, streamed tiles with caves and
// materials, with a fixed climate and no cave districts.

#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "golden_support.hpp"
#include "test_assert.hpp"

#include <cstdint>
#include <vector>

namespace golden {

namespace cave_detail {

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// A temperate, humid climate everywhere: wet aquifers and speleothems without the cost of the
// climate fields.
inline terrain::CaveGround groundOf(const terrain::Heightfield& field) {
    terrain::CaveGround ground;
    ground.height = [&field](double x, double z) { return field.heightAt(x, z); };
    ground.environment = [](double, double) {
        return terrain::CaveEnvironment{terrain::Biome::TemperateForest, 11.0f, 0.75f};
    };
    return ground;
}

inline double unit(std::uint64_t stream, std::uint64_t index) {
    return static_cast<double>(terrain::noise::deriveSeed(stream, index) >> 40) / 16777216.0;
}

inline void addVoids(Checksum& sum, const terrain::CaveVoids& v) {
    for (const float value : {v.spaghetti, v.noodles, v.cheese, v.ravines, v.entrances, v.limit,
                              v.entranceLimit, v.barrier}) {
        sum.add(value);
    }
}

inline void addColumn(Checksum& sum, const terrain::CaveColumn& c) {
    sum.add(c.ground);
    sum.add(c.exclusion);
    sum.add(c.ravineDistance);
    sum.add(c.ravineStrength);
    sum.add(c.entranceCount);
    sum.add(c.waterLevel);
    sum.add(c.frozen);
    sum.add(c.barrierCount);
}

// Voids, combined void, carved density and water at 2500 points of each of 8 boxes of 64 m,
// for the detail of a LOD.
inline std::uint64_t pointsChecksum(const terrain::CaveField& field,
                                    const terrain::CaveGround& ground, int lod) {
    Checksum sum;
    const terrain::CaveDetail detail = terrain::caveDetailForLod(lod);
    for (std::uint64_t box = 0; box < 8; ++box) {
        const double x0 = -2600.0 + 700.0 * static_cast<double>(box);
        const double z0 = -4100.0 + 450.0 * static_cast<double>(box % 3);
        terrain::CaveSampler sampler(field, ground, detail,
                                     {x0, -1200.0, z0, x0 + 64.0, 1600.0, z0 + 64.0});
        for (std::uint64_t n = 0; n < 2500; ++n) {
            const double x = x0 + 64.0 * unit(box, 3 * n);
            const double z = z0 + 64.0 * unit(box, 3 * n + 1);
            const float height = ground.height(x, z);
            const double y = static_cast<double>(height) - 105.0 + 110.0 * unit(box, 3 * n + 2);
            const terrain::CaveColumn column = sampler.column(x, z, height);
            const auto terrainDensity = static_cast<float>(y - static_cast<double>(height));
            const float carved = sampler.density(column, y, terrainDensity);
            addColumn(sum, column);
            addVoids(sum, sampler.voids(column, y));
            sum.add(sampler.voidAt(column, y));
            sum.add(carved);
            sum.add(sampler.waterDensity(column, y, terrainDensity, carved));
        }
    }
    return sum.value();
}

inline std::uint64_t aquifersChecksum(const terrain::CaveField& field,
                                      const terrain::CaveGround& ground) {
    Checksum sum;
    for (int cellZ = -6; cellZ < 6; ++cellZ) {
        for (int cellX = -6; cellX < 6; ++cellX) {
            const terrain::AquiferCell cell = field.aquifer(cellX, cellZ, ground);
            sum.add(cell.siteX);
            sum.add(cell.siteZ);
            sum.add(cell.wet);
            sum.add(cell.frozen);
            sum.add(cell.level);
        }
    }
    return sum.value();
}

inline std::uint64_t entrancesChecksum(const std::vector<terrain::CaveEntrance>& entrances) {
    Checksum sum;
    for (const terrain::CaveEntrance& e : entrances) {
        for (const double value : {e.topX, e.topY, e.topZ, e.bottomX, e.bottomY, e.bottomZ}) {
            sum.add(value);
        }
        sum.add(e.tubeRadius);
        sum.add(e.chamberRadius);
        sum.add(e.cellX);
        sum.add(e.cellZ);
    }
    return sum.value();
}

// Speleothems of a 96 m square at LOD 0.
inline std::uint64_t speleothemsChecksum(const terrain::CaveField& field,
                                         const terrain::CaveGround& ground) {
    constexpr double x0 = 1000.0;
    constexpr double z0 = -2000.0;
    constexpr double side = 96.0;
    terrain::CaveSampler sampler(field, ground, terrain::caveDetailForLod(0),
                                 {x0, -1200.0, z0, x0 + side, 4000.0, z0 + side});
    const terrain::SpeleothemField speleothems(kSeed);
    const std::vector<terrain::Speleothem> placed =
        speleothems.place(sampler, x0, z0, x0 + side, z0 + side);
    TEST_CHECK(placed.size() > 10);
    Checksum sum;
    for (const terrain::Speleothem& s : placed) {
        addSpeleothem(sum, s);
    }
    return sum.value();
}

inline std::uint64_t tilesChecksum(const terrain::CaveEntrance& entrance) {
    const terrain::TerrainTiles tiles(kSeed, {}, {}, terrain::CaveParams{});
    const terrain::ChunkCoord chunk = terrain::worldToChunk(entrance.bottomX, entrance.bottomZ);
    constexpr unsigned kAllSeams = 0xFFU;
    Checksum sum;
    addTile(sum, tiles.generate({{chunk, 0}, kAllSeams}));
    addTile(sum, tiles.generate({terrain::tileContaining(chunk, 1), terrain::kSeamNegX}));
    addTile(sum, tiles.generate({terrain::tileContaining(chunk, 2), 0}));
    return sum.value();
}

inline std::uint64_t materialsChecksum() {
    Checksum sum;
    for (int biome = 0; biome < terrain::kBiomeCount; ++biome) {
        for (const float temperature : {-12.0f, -2.0f, 4.0f, 18.0f, 30.0f}) {
            for (const float humidity : {0.05f, 0.45f, 0.62f, 0.95f}) {
                for (const float depth : {0.5f, 10.0f, 25.0f, 50.0f, 80.0f}) {
                    for (const float normalY : {-0.9f, 0.0f, 0.8f}) {
                        terrain::CaveWallSite site;
                        site.environment = {static_cast<terrain::Biome>(biome), temperature,
                                            humidity};
                        site.depth = depth;
                        site.normalY = normalY;
                        site.aboveWater = depth > 40.0f ? 1.0f : 1.0e9f;
                        site.frozenWater = temperature < 0.0f;
                        sum.add(static_cast<std::uint64_t>(terrain::caveMaterial(site)), 1);
                    }
                }
            }
        }
    }
    return sum.value();
}

} // namespace cave_detail

// Computes the cave parts with their expected values.
inline std::vector<Part> caveParts() {
    using namespace cave_detail;
    const terrain::Heightfield heightfield(kSeed);
    const terrain::CaveGround ground = groundOf(heightfield);
    const terrain::CaveField field(kSeed);
    const std::vector<terrain::CaveEntrance> entrances =
        field.entrancesNear(-750.0, -4750.0, 750.0, -3250.0, ground);
    TEST_CHECK(!entrances.empty());
    const std::uint64_t tiles = entrances.empty() ? 0 : tilesChecksum(entrances.front());
    return {
        {"cave points LOD 0", pointsChecksum(field, ground, 0), 0xC1B49F441628B500ULL},
        {"cave points LOD 1", pointsChecksum(field, ground, 1), 0x847399965D4D927AULL},
        {"cave points LOD 2", pointsChecksum(field, ground, 2), 0x2A7153C19E0A1837ULL},
        {"aquifers", aquifersChecksum(field, ground), 0x49504A9D474AA707ULL},
        {"entrances", entrancesChecksum(entrances), 0xCBC03D57177FAB70ULL},
        {"speleothems", speleothemsChecksum(field, ground), 0x8E535CD1327FED4AULL},
        {"cave tiles", tiles, 0x77B171615DE6029FULL},
        {"cave materials", materialsChecksum(), 0xD9C5568AB1EF9B55ULL},
    };
}

} // namespace golden
