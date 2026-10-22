#pragma once

// Golden checksums of the caves (see golden_support.hpp): voids, densities and water of LOD 0
// to 2 at fixed points, aquifer cells, entrances, speleothems, streamed tiles with caves and
// materials, with a fixed climate and no cave districts; and the points, aquifer cells and
// entrances across the corner of the wrapped world, where its four edges meet.

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

#include <algorithm>
#include <cstdint>
#include <vector>

namespace golden {

namespace cave_detail {

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// The parts are taken around (kAreaX, kAreaZ): land of the default world, a few kilometers from
// the coast of the origin, with entrances and speleothems where the parts look for them (the
// first point of a lattice of 3,072 m, by rings around the origin, to have them). kAreaCellX and
// kAreaCellZ: its aquifer cell (cells of 192 m).
constexpr double kAreaX = -9216.0;
constexpr double kAreaZ = 6144.0;
constexpr int kAreaCellX = -48;
constexpr int kAreaCellZ = 32;

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

// Voids, combined void, carved density and water at 2500 points (drawn from the stream box) of
// the square of 64 m from (x0, z0), from 105 m under the ground to 5 m over it, for a detail.
inline void addBox(Checksum& sum, const terrain::CaveField& field,
                   const terrain::CaveGround& ground, const terrain::CaveDetail& detail, double x0,
                   double z0, std::uint64_t box) {
    float low = ground.height(x0, z0);
    float high = low;
    for (std::uint64_t n = 0; n < 2500; ++n) {
        const float height =
            ground.height(x0 + 64.0 * unit(box, 3 * n), z0 + 64.0 * unit(box, 3 * n + 1));
        low = std::min(low, height);
        high = std::max(high, height);
    }
    terrain::CaveSampler sampler(field, ground, detail,
                                 {x0, static_cast<double>(low) - 106.0, z0, x0 + 64.0,
                                  static_cast<double>(high) + 6.0, z0 + 64.0});
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

// The points of 8 boxes of 64 m, for the detail of a LOD.
inline std::uint64_t pointsChecksum(const terrain::CaveField& field,
                                    const terrain::CaveGround& ground, int lod) {
    Checksum sum;
    for (std::uint64_t box = 0; box < 8; ++box) {
        addBox(sum, field, ground, terrain::caveDetailForLod(lod),
               kAreaX - 2600.0 + 700.0 * static_cast<double>(box),
               kAreaZ - 4100.0 + 450.0 * static_cast<double>(box % 3), box);
    }
    return sum.value();
}

// The aquifer cells [-6, 6)^2 around the cell (centerX, centerZ).
inline std::uint64_t aquifersChecksum(const terrain::CaveField& field,
                                      const terrain::CaveGround& ground, int centerX, int centerZ) {
    Checksum sum;
    for (int cellZ = centerZ - 6; cellZ < centerZ + 6; ++cellZ) {
        for (int cellX = centerX - 6; cellX < centerX + 6; ++cellX) {
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
    constexpr double x0 = kAreaX + 1000.0;
    constexpr double z0 = kAreaZ - 2000.0;
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

// Across the corner (world.x / 2, world.z / 2): the points of a box of 64 m centered on it at
// LOD 0, the aquifer cells around it and the entrances within 1.5 km of it.
inline std::uint64_t cornerChecksum(const terrain::CaveField& field,
                                    const terrain::CaveGround& ground) {
    const double x = field.world().x / 2.0;
    const double z = field.world().z / 2.0;
    Checksum sum;
    addBox(sum, field, ground, terrain::caveDetailForLod(0), x - 32.0, z - 32.0, 8);
    sum.add(aquifersChecksum(field, ground, static_cast<int>(field.aquiferCellsX().count / 2),
                             static_cast<int>(field.aquiferCellsZ().count / 2)));
    sum.add(entrancesChecksum(
        field.entrancesNear(x - 1500.0, z - 1500.0, x + 1500.0, z + 1500.0, ground)));
    return sum.value();
}

} // namespace cave_detail

// Computes the cave parts with their expected values.
inline std::vector<Part> caveParts() {
    using namespace cave_detail;
    const terrain::Heightfield heightfield(kSeed);
    const terrain::CaveGround ground = groundOf(heightfield);
    const terrain::CaveField field(kSeed);
    const std::vector<terrain::CaveEntrance> entrances = field.entrancesNear(
        kAreaX - 750.0, kAreaZ - 4750.0, kAreaX + 750.0, kAreaZ - 3250.0, ground);
    TEST_CHECK(!entrances.empty());
    const std::uint64_t tiles = entrances.empty() ? 0 : tilesChecksum(entrances.front());
    return {
        {"cave points LOD 0", pointsChecksum(field, ground, 0), 0x348F650A4753DB4BULL},
        {"cave points LOD 1", pointsChecksum(field, ground, 1), 0x307E5A0F59959E86ULL},
        {"cave points LOD 2", pointsChecksum(field, ground, 2), 0x24C7B7014A49C3CEULL},
        {"aquifers", aquifersChecksum(field, ground, kAreaCellX, kAreaCellZ),
         0x0734F8474122135FULL},
        {"entrances", entrancesChecksum(entrances), 0x33FBACDA5B4BFDE8ULL},
        {"speleothems", speleothemsChecksum(field, ground), 0x99C239CD316CC1D1ULL},
        {"cave tiles", tiles, 0x049C239C8A011757ULL},
        {"cave materials", materialsChecksum(), 0xD9C5568AB1EF9B55ULL},
        {"caves at the corner", cornerChecksum(field, ground), 0xFDDF1A7643D2985CULL},
    };
}

} // namespace golden
