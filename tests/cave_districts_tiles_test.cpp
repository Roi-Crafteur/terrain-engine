// Streamed tiles with cave districts (Phase 8C-1), on the default world: deterministic whatever
// the instance, the far LOD 3 unchanged, the districts changing the tiles that LOD 0 to 2 carve,
// the profile of a point and the carved density independent of the tile that computes them, the
// cave flora of the lush districts on LOD 0 tiles only, and the materials of the districts (ice,
// basalt, moss, frozen lakes) on the walls of the tiles. Seams: cave_districts_seams_test.cpp.
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_flora.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_districts_carving_support.hpp"
#include "cave_districts_streaming_support.hpp"
#include "speleothems_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace {

using districts_test::defaultCaves;
using districts_test::defaultProfile;
using districts_test::DistrictEntrance;
using districts_test::uniformCaves;
using streaming_test::kSeed;
using terrain::CaveDistrictType;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveProfile;
using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;

// ---------------------------------------------------------------------------

// The same tiles from the same request, twice, and from another instance of the same world, at
// the four LODs and with seams; another seed, other caves. The far LOD 3 carves nothing: it is
// the tile of the world without caves, and of the caves without districts. The tiles LOD 0 to 2
// carve differ from the ones without districts at most of the entrances of the main districts.
void testDeterministic(const TerrainTiles& tiles, const std::vector<DistrictEntrance>& entrances) {
    const TerrainTiles again(kSeed, {}, {}, defaultCaves());
    const TerrainTiles plain(kSeed, {}, {}, terrain::CaveParams{});
    const TerrainTiles world(kSeed);
    const TerrainTiles other(kSeed + 1, {}, {}, defaultCaves());
    const ChunkCoord chunk = streaming_test::entranceChunk(entrances.front().entrance);
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const terrain::TileRequest request{terrain::tileContaining(chunk, lod),
                                           terrain::kSeamNegX | terrain::kSeamPosZ};
        const TileData data = tiles.generate(request);
        TEST_CHECK(!data.mesh.indices.empty());
        TEST_CHECK(data == tiles.generate(request));
        TEST_CHECK(data == again.generate(request));
        TEST_CHECK(data.caveFlora.empty() || lod == 0);
        if (lod == terrain::kMaxLod) {
            TEST_CHECK(data == plain.generate(request) && data == world.generate(request));
        } else {
            TEST_CHECK(data.mesh != other.generate(request).mesh);
        }
    }
    std::size_t differing = 0;
    std::size_t checked = 0;
    for (std::size_t n = 0; n < entrances.size() && checked < 12; n += 3, ++checked) {
        const TileKey key{streaming_test::entranceChunk(entrances[n].entrance), 0};
        differing += tiles.generate({key, 0}) != plain.generate({key, 0}) ? 1 : 0;
    }
    std::printf("districts: %zu of %zu LOD 0 tiles at entrances differ from the ones without\n",
                differing, checked);
    TEST_CHECK(checked == 12 && differing * 4 >= checked * 3);
}

// ---------------------------------------------------------------------------

// Points of the lattice of the world in the tile of the chunk moved by shift chunks, from 12 to
// 90 m under the ground.
std::vector<caves_test::Point> latticePoints(const TerrainTiles& tiles, ChunkCoord chunk,
                                             int shift) {
    caves_test::Rng rng(static_cast<std::uint64_t>(60 + shift));
    const double x0 = (chunk.x + shift) * terrain::kChunkSizeMeters;
    const double z0 = (chunk.z - shift) * terrain::kChunkSizeMeters;
    std::vector<caves_test::Point> points;
    for (int n = 0; n < 60; ++n) {
        const double x = std::floor(x0 + rng.range(0.0, terrain::kChunkSizeMeters));
        const double z = std::floor(z0 + rng.range(0.0, terrain::kChunkSizeMeters));
        const float ground = tiles.heightfield().heightAt(x, z);
        points.push_back(
            {x, std::floor(static_cast<double>(ground) - rng.range(12.0, 90.0)), z, ground});
    }
    return points;
}

// The profile of a point, and the carved density there, do not depend on the area being
// generated: a sampler over the points of a tile and one over the tile and its eight neighbors
// give the very values of the field alone, and the density of the lattice of the world is the
// carved density of LOD 0.
void testProfileDoesNotDependOnTheTile(const TerrainTiles& tiles, const CaveEntrance& entrance) {
    const CaveField& field = *tiles.caves();
    const terrain::CaveGround& ground = tiles.caveGround();
    terrain::AquiferCache alone(field, ground);
    bool profiles = true;
    bool densities = true;
    std::size_t points = 0;
    std::size_t carved = 0;
    for (const int shift : {0, 1, 5, 40}) {
        const std::vector<caves_test::Point> list =
            latticePoints(tiles, streaming_test::entranceChunk(entrance), shift);
        terrain::CaveBox wide = caves_test::boxOf(list);
        wide = {wide.minX - 16.0, wide.minY - 8.0, wide.minZ - 16.0,
                wide.maxX + 16.0, wide.maxY + 8.0, wide.maxZ + 16.0};
        terrain::CaveSampler narrow(field, ground, terrain::CaveDetail{}, caves_test::boxOf(list));
        terrain::CaveSampler around(field, ground, terrain::CaveDetail{}, wide);
        for (const caves_test::Point& p : list) {
            const float rock = caves_test::terrainDensity(p.y, p.ground);
            const terrain::CaveColumn a = narrow.column(p.x, p.z, p.ground);
            const terrain::CaveColumn b = around.column(p.x, p.z, p.ground);
            const float density = narrow.density(a, p.y, rock);
            profiles = profiles && a.profile == b.profile &&
                       a.profile == alone.profileAt(p.x, p.z) &&
                       a.profile == field.column(p.x, p.z, p.ground, ground, {}).profile &&
                       narrow.profileAt(p.x, p.z) == around.profileAt(p.x, p.z);
            densities = densities && density == around.density(b, p.y, rock) &&
                        density == terrain::caveDensityAt(field, ground, terrain::CaveDetail{}, p.x,
                                                          p.y, p.z) &&
                        density == tiles.densityAt(static_cast<int>(p.x), static_cast<int>(p.y),
                                                   static_cast<int>(p.z));
            ++points;
            carved += density != rock ? 1 : 0;
        }
    }
    std::printf("stability: %zu points over 4 tiles, %zu carved\n", points, carved);
    TEST_CHECK(profiles && densities && carved >= 20);
}

// ---------------------------------------------------------------------------

struct FloraSpot {
    ChunkCoord chunk;
    std::vector<terrain::CaveFlora> flora;
};

// The first chunk around the site of the first lush district on land that holds at least 3 cave
// plants, with the plants placed on it by the field alone.
std::optional<FloraSpot> findFlora(const TerrainTiles& tiles) {
    const CaveField& field = *tiles.caves();
    const terrain::CaveGround& ground = tiles.caveGround();
    const std::vector<terrain::CaveDistrict> lush =
        districts_test::districtsOf(field, ground, {-8, 17}, CaveDistrictType::Lush, 20.0f);
    if (lush.empty()) {
        return std::nullopt;
    }
    const terrain::CaveFloraField flora(kSeed, field.districts()->params().flora);
    const ChunkCoord center = terrain::worldToChunk(lush.front().siteX, lush.front().siteZ);
    for (int dz = -6; dz <= 6; ++dz) {
        for (int dx = -6; dx <= 6; ++dx) {
            const ChunkCoord chunk{center.x + dx, center.z + dz};
            const speleothems_test::Square square{chunk.x * terrain::kChunkSizeMeters,
                                                  chunk.z * terrain::kChunkSizeMeters,
                                                  terrain::kChunkSizeMeters};
            terrain::CaveSampler sampler = speleothems_test::samplerOver(field, ground, square);
            std::vector<terrain::CaveFlora> placed =
                flora.place(sampler, square.minX, square.minZ, square.minX + square.size,
                            square.minZ + square.size);
            if (placed.size() >= 3) {
                return FloraSpot{chunk, std::move(placed)};
            }
        }
    }
    return std::nullopt;
}

// Cave flora (ferns, mushrooms, vines) of the lush districts: the LOD 0 tile of a chunk over a
// lush district holds the plants the field places there, and no tile of the coarser LODs holds
// any; neither do the tiles of the world without districts or without caves, nor the LOD 0 tiles
// at the entrances of the other districts.
void testCaveFlora(const TerrainTiles& tiles, const std::vector<DistrictEntrance>& pure) {
    const TerrainTiles plain(kSeed, {}, {}, terrain::CaveParams{});
    const TerrainTiles world(kSeed);
    const std::optional<FloraSpot> spot = findFlora(tiles);
    TEST_CHECK(spot.has_value());
    if (!spot) {
        return;
    }
    const TileData lod0 = tiles.generate({{spot->chunk, 0}, 0});
    const double x0 = spot->chunk.x * terrain::kChunkSizeMeters;
    const double z0 = spot->chunk.z * terrain::kChunkSizeMeters;
    const bool inside =
        std::all_of(lod0.caveFlora.begin(), lod0.caveFlora.end(), [&](const auto& f) {
            return f.x >= x0 && f.x < x0 + terrain::kChunkSizeMeters && f.z >= z0 &&
                   f.z < z0 + terrain::kChunkSizeMeters;
        });
    std::printf("cave flora: %zu plants on the LOD 0 tile of chunk (%d, %d)\n",
                lod0.caveFlora.size(), spot->chunk.x, spot->chunk.z);
    TEST_CHECK(lod0.caveFlora == spot->flora && inside);
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        for (const unsigned seams : {0U, terrain::kSeamNegX | terrain::kSeamPosZ}) {
            const terrain::TileRequest request{terrain::tileContaining(spot->chunk, lod), seams};
            TEST_CHECK(lod == 0 ? !tiles.generate(request).caveFlora.empty()
                                : tiles.generate(request).caveFlora.empty());
            TEST_CHECK(plain.generate(request).caveFlora.empty());
            TEST_CHECK(world.generate(request).caveFlora.empty());
        }
    }
    std::size_t others = 0;
    for (std::size_t n = 0; n < pure.size() && others < 6; ++n) {
        if (pure[n].type != CaveDistrictType::Lush) {
            const TileKey key{streaming_test::entranceChunk(pure[n].entrance), 0};
            TEST_CHECK(tiles.generate({key, 0}).caveFlora.empty());
            ++others;
        }
    }
    TEST_CHECK(others == 6);
}

// ---------------------------------------------------------------------------

std::size_t countOf(const std::vector<terrain::CaveMaterial>& materials, terrain::CaveMaterial m) {
    return static_cast<std::size_t>(std::count(materials.begin(), materials.end(), m));
}

// The materials of the walls of a tile (at the chamber of the first entrance, deep enough for the
// walls under mossDepth) follow the profile of the district: the glacier districts paint ice (and
// freeze their lakes), the volcanic ones basalt, the lush ones moss, where the neutral districts,
// over the very same caves, paint none of the first two.
void testMaterials(const std::vector<DistrictEntrance>& pure) {
    using terrain::CaveMaterial;
    const CaveEntrance& entrance = pure.front().entrance;
    const ChunkCoord chunk = terrain::worldToChunk(entrance.bottomX, entrance.bottomZ);
    const auto tileOf = [chunk](CaveDistrictType type) {
        const TerrainTiles tiles(kSeed, {}, {}, uniformCaves(defaultProfile(type)));
        return tiles.generate({{chunk, 0}, 0});
    };
    const TerrainTiles neutralTiles(kSeed, {}, {}, uniformCaves(CaveProfile{}));
    const TileData neutral = neutralTiles.generate({{chunk, 0}, 0});
    const TileData glacier = tileOf(CaveDistrictType::Glacier);
    const TileData volcanic = tileOf(CaveDistrictType::Volcanic);
    const TileData lush = tileOf(CaveDistrictType::Lush);
    std::printf("materials: ice %zu / %zu (glacier / neutral), basalt %zu / %zu (volcanic / "
                "neutral), moss %zu / %zu (lush / neutral)\n",
                countOf(glacier.materials, CaveMaterial::Ice),
                countOf(neutral.materials, CaveMaterial::Ice),
                countOf(volcanic.materials, CaveMaterial::Basalt),
                countOf(neutral.materials, CaveMaterial::Basalt),
                countOf(lush.materials, CaveMaterial::Moss),
                countOf(neutral.materials, CaveMaterial::Moss));
    TEST_CHECK(countOf(neutral.materials, CaveMaterial::Ice) == 0);
    TEST_CHECK(countOf(glacier.materials, CaveMaterial::Ice) > 0);
    TEST_CHECK(countOf(neutral.materials, CaveMaterial::Basalt) == 0);
    TEST_CHECK(countOf(glacier.materials, CaveMaterial::Basalt) == 0);
    TEST_CHECK(countOf(volcanic.materials, CaveMaterial::Basalt) > 0);
    TEST_CHECK(countOf(lush.materials, CaveMaterial::Moss) >
               countOf(neutral.materials, CaveMaterial::Moss));
    // The geometry is the neutral one in the glacier districts: only the materials change.
    TEST_CHECK(glacier.mesh == neutral.mesh && glacier.materials != neutral.materials);
}

// The lakes of a glacier district are frozen: over a chunk with water, the very same lakes are
// ice instead of water.
void testFrozenLakes(const std::vector<DistrictEntrance>& pure) {
    using terrain::CaveMaterial;
    const TerrainTiles neutralTiles(kSeed, {}, {}, uniformCaves(CaveProfile{}));
    const TerrainTiles glacierTiles(kSeed, {}, {},
                                    uniformCaves(defaultProfile(CaveDistrictType::Glacier)));
    for (std::size_t n = 0; n < std::min<std::size_t>(pure.size(), 24); ++n) {
        const TileKey key{streaming_test::entranceChunk(pure[n].entrance), 0};
        const TileData neutral = neutralTiles.generate({key, 0});
        if (neutral.water.vertices.empty()) {
            continue;
        }
        const TileData glacier = glacierTiles.generate({key, 0});
        std::printf("lakes: %zu water vertices, ice instead of water in the glacier districts\n",
                    neutral.water.vertices.size());
        TEST_CHECK(glacier.water == neutral.water);
        TEST_CHECK(countOf(neutral.waterMaterials, CaveMaterial::Water) ==
                   neutral.waterMaterials.size());
        TEST_CHECK(countOf(glacier.waterMaterials, CaveMaterial::Ice) ==
                   glacier.waterMaterials.size());
        return;
    }
    TEST_CHECK(false);
}

} // namespace

int main() {
    const TerrainTiles tiles(kSeed, {}, {}, defaultCaves());
    const std::vector<DistrictEntrance> entrances = districts_test::districtEntrances(tiles);
    const std::vector<DistrictEntrance> pure = districts_test::select(entrances, false);
    std::printf("%zu entrances near the origin, %zu in the core of a district\n", entrances.size(),
                pure.size());
    TEST_CHECK(pure.size() >= 30 && entrances.size() - pure.size() >= 10);
    if (pure.size() < 30) {
        return test::result();
    }
    testDeterministic(tiles, pure);
    testProfileDoesNotDependOnTheTile(tiles, pure.front().entrance);
    testCaveFlora(tiles, pure);
    testMaterials(pure);
    testFrozenLakes(pure);
    return test::result();
}
