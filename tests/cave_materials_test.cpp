// Materials of the caves: the priority of the rules of caveMaterial, the names, the environment
// of the heightfield (deterministic nodes, bilinear climate, the cache), and the streamed tiles
// with caves: one material per vertex of the terrain and of the lakes, the outer surface kept,
// the lakes in the cave air under the ground, the speleothems of the LOD 0 tiles, and none of
// it changing the geometry or existing without caves or on the far LODs.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_environment_test_support.hpp"
#include "cave_materials_test_support.hpp"
#include "caves_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using cave_materials_test::checkLake;
using cave_materials_test::countSurface;
using cave_materials_test::entranceChunks;
using cave_materials_test::findLake;
using cave_materials_test::Lake;
using cave_materials_test::LakeChecks;
using cave_materials_test::onlyWaterOrIce;
using cave_materials_test::requestOf;
using cave_materials_test::Slope;
using cave_materials_test::slopeAround;
using cave_materials_test::speleothemsMatch;
using cave_materials_test::stoneWall;
using cave_materials_test::SurfaceCounts;
using cave_materials_test::withBiome;
using cave_materials_test::World;
using caves_test::Rng;
using terrain::Biome;
using terrain::CaveEntrance;
using terrain::CaveEnvironment;
using terrain::CaveEnvironmentCache;
using terrain::CaveMaterial;
using terrain::CaveMaterialParams;
using terrain::CaveWallSite;
using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;

constexpr std::uint64_t kSeed = streaming_test::kSeed;

// ---------------------------------------------------------------------------

// The first rules by priority, at their thresholds (depths below the ground, temperatures and
// heights inclusive on the side of the rule): the outer surface beats ice, ice beats mud, mud
// beats sandstone.
void testSurfaceIceAndMudRules() {
    const CaveMaterialParams p;
    const auto pick = [&p](const CaveWallSite& s) { return caveMaterial(s, p); };
    const float cold = p.iceTemperature - 5.0f;
    CaveWallSite site = stoneWall();
    TEST_CHECK(pick(site) == CaveMaterial::Stone);

    // Everything that triggers a rule: the surface, ice, mud, sandstone, moss, gravel, deep
    // stone and limestone, in a cold desert (moss aside), on a floor by a frozen lake.
    CaveWallSite all = withBiome(stoneWall(), Biome::Desert, cold, 0.9f);
    all.normalY = 1.0f;
    all.aboveWater = 0.0f;
    all.frozenWater = true;
    all.depth = p.surfaceDepth - 0.01f;
    TEST_CHECK(pick(all) == CaveMaterial::Surface);
    all.depth = p.surfaceDepth;
    TEST_CHECK(pick(all) == CaveMaterial::Ice);

    // Ice: a frozen lake shore (wet height inclusive), or a cold cave down to iceDepth.
    all.environment.temperature = 12.0f;
    all.aboveWater = p.wetHeight;
    TEST_CHECK(pick(all) == CaveMaterial::Ice);
    all.aboveWater = p.wetHeight + 0.01f;
    TEST_CHECK(pick(all) == CaveMaterial::Sandstone);
    all.frozenWater = false;
    all.aboveWater = 0.0f;
    all.environment.temperature = p.iceTemperature;
    all.depth = p.iceDepth;
    TEST_CHECK(pick(all) == CaveMaterial::Ice);
    all.environment.temperature = std::nextafter(p.iceTemperature, 100.0f);
    TEST_CHECK(pick(all) == CaveMaterial::Mud);
    all.environment.temperature = p.iceTemperature;
    all.depth = p.iceDepth + 0.01f;
    TEST_CHECK(pick(all) == CaveMaterial::Mud);
    CaveWallSite shallowCold = withBiome(stoneWall(), Biome::Taiga, cold, 0.9f);
    shallowCold.depth = p.iceDepth - 5.0f;
    TEST_CHECK(pick(shallowCold) == CaveMaterial::Ice);
    shallowCold.depth = p.iceDepth + 5.0f;
    TEST_CHECK(pick(shallowCold) != CaveMaterial::Ice);

    // Mud: next to a lake that is not frozen, under the lake level too, before sandstone and
    // gravel; the lake in the same climate but further away gives them back.
    all.environment.temperature = 12.0f;
    all.aboveWater = -5.0f;
    TEST_CHECK(pick(all) == CaveMaterial::Mud);
    all.aboveWater = 1.0e9f;
    TEST_CHECK(pick(all) == CaveMaterial::Sandstone);
}

// The rules of the rock, each beating the next: sandstone under the dry biomes, moss near the
// surface under humid forests and marshes, gravel on floors, deepstone deep down, limestone in
// humid climates, stone otherwise.
void testRockRules() {
    const CaveMaterialParams p;
    const auto pick = [&p](const CaveWallSite& s) { return caveMaterial(s, p); };
    CaveWallSite floorDeep = stoneWall();
    floorDeep.normalY = 1.0f;
    floorDeep.depth = p.deepDepth + 10.0f;
    for (const Biome dry : {Biome::Desert, Biome::Savanna, Biome::Beach}) {
        TEST_CHECK(pick(withBiome(floorDeep, dry, 12.0f, 0.9f)) == CaveMaterial::Sandstone);
    }
    for (const Biome other : {Biome::Grassland, Biome::Tundra, Biome::Mountain, Biome::Snow}) {
        TEST_CHECK(pick(withBiome(floorDeep, other, 12.0f, 0.9f)) == CaveMaterial::Gravel);
    }

    // Moss: a mossy biome, humid enough, at most mossDepth deep, before gravel.
    CaveWallSite moss = withBiome(stoneWall(), Biome::TemperateForest, 12.0f, p.mossHumidity);
    moss.depth = p.mossDepth;
    moss.normalY = 1.0f;
    for (const Biome mossy :
         {Biome::TemperateForest, Biome::TropicalForest, Biome::Marsh, Biome::Taiga}) {
        TEST_CHECK(pick(withBiome(moss, mossy, 12.0f, p.mossHumidity)) == CaveMaterial::Moss);
    }
    for (const Biome other : {Biome::Grassland, Biome::Tundra, Biome::Mountain, Biome::Ocean}) {
        TEST_CHECK(pick(withBiome(moss, other, 12.0f, 1.0f)) == CaveMaterial::Gravel);
    }
    moss.depth = p.mossDepth + 0.01f;
    TEST_CHECK(pick(moss) == CaveMaterial::Gravel);
    moss.depth = p.mossDepth;
    moss.environment.humidity = std::nextafter(p.mossHumidity, 0.0f);
    TEST_CHECK(pick(moss) == CaveMaterial::Gravel);

    // Gravel on floors (the normal points up at least floorNormal), before deepstone and
    // limestone; deepstone from deepDepth; limestone from limestoneHumidity; stone otherwise.
    CaveWallSite wall = withBiome(stoneWall(), Biome::Grassland, 12.0f, 1.0f);
    wall.depth = p.deepDepth + 1.0f;
    wall.normalY = p.floorNormal;
    TEST_CHECK(pick(wall) == CaveMaterial::Gravel);
    wall.normalY = std::nextafter(p.floorNormal, 0.0f);
    TEST_CHECK(pick(wall) == CaveMaterial::Deepstone);
    wall.depth = p.deepDepth;
    TEST_CHECK(pick(wall) == CaveMaterial::Deepstone);
    wall.depth = std::nextafter(p.deepDepth, 0.0f);
    TEST_CHECK(pick(wall) == CaveMaterial::Limestone);
    wall.environment.humidity = p.limestoneHumidity;
    TEST_CHECK(pick(wall) == CaveMaterial::Limestone);
    wall.environment.humidity = std::nextafter(p.limestoneHumidity, 0.0f);
    TEST_CHECK(pick(wall) == CaveMaterial::Stone);
}

// The thresholds are read from the parameters.
void testParameters() {
    CaveMaterialParams q;
    CaveWallSite site = stoneWall();
    site.depth = 5.0f;
    TEST_CHECK(caveMaterial(site, q) == CaveMaterial::Stone);
    q.surfaceDepth = 6.0f;
    TEST_CHECK(caveMaterial(site, q) == CaveMaterial::Surface);
    q.surfaceDepth = 1.5f;
    q.deepDepth = 5.0f;
    TEST_CHECK(caveMaterial(site, q) == CaveMaterial::Deepstone);
    q.deepDepth = 70.0f;
    q.limestoneHumidity = 0.2f;
    TEST_CHECK(caveMaterial(site, q) == CaveMaterial::Limestone);
    q.floorNormal = -1.0f;
    TEST_CHECK(caveMaterial(site, q) == CaveMaterial::Gravel);
    site.aboveWater = 3.5f;
    q.wetHeight = 4.0f;
    TEST_CHECK(caveMaterial(site, q) == CaveMaterial::Mud);
    site.environment.temperature = -3.0f;
    q.iceTemperature = -4.0f;
    q.iceDepth = 1.0f;
    TEST_CHECK(caveMaterial(site, q) == CaveMaterial::Mud);
    q.iceDepth = 10.0f;
    q.iceTemperature = -3.0f;
    TEST_CHECK(caveMaterial(site, q) == CaveMaterial::Ice);
}

// Every material has its own lowercase name.
void testNames() {
    std::set<std::string> names;
    bool lowercase = true;
    for (int n = 0; n < terrain::kCaveMaterialCount; ++n) {
        const std::string name = terrain::caveMaterialName(static_cast<CaveMaterial>(n));
        names.insert(name);
        lowercase = lowercase && !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
                        return c >= 'a' && c <= 'z';
                    });
    }
    TEST_CHECK(names.size() == static_cast<std::size_t>(terrain::kCaveMaterialCount));
    TEST_CHECK(lowercase);
    TEST_CHECK(std::string(terrain::caveMaterialName(CaveMaterial::Surface)) == "surface");
    TEST_CHECK(std::string(terrain::caveMaterialName(CaveMaterial::Water)) == "water");
}

// ---------------------------------------------------------------------------

constexpr std::array<std::array<std::int64_t, 2>, 8> kNodes = {
    {{0, 0}, {3, -7}, {-50, 20}, {125, -125}, {-1, -1}, {62, 125}, {-125, 62}, {7, 7}}};

// Nodes are deterministic and depend on the seed; at() gives the node at the node positions,
// including negative ones; temperature and humidity stay physical.
void testNodes() {
    const World a(kSeed);
    const World b(kSeed);
    const World other(kSeed + 1);
    bool same = true;
    bool atNodes = true;
    bool physical = true;
    int differing = 0;
    for (const auto& [i, k] : kNodes) {
        const CaveEnvironment node = a.field.node(i, k);
        same = same && node == b.field.node(i, k);
        differing += node != other.field.node(i, k) ? 1 : 0;
        const double x = static_cast<double>(i) * terrain::kCaveEnvironmentSpacing;
        const double z = static_cast<double>(k) * terrain::kCaveEnvironmentSpacing;
        atNodes = atNodes && a.field.at(x, z) == node;
        physical = physical && node.humidity >= 0.0f && node.humidity <= 1.0f &&
                   node.temperature > -80.0f && node.temperature < 60.0f;
    }
    std::printf("environment nodes: %d of %zu differ with seed + 1\n", differing, kNodes.size());
    TEST_CHECK(same && atNodes && physical);
    TEST_CHECK(differing >= 4);
}

// Between four nodes, temperature and humidity are bilinear (the center is the mean of the
// nodes, a step of s meters changes them by at most s / spacing times the largest difference of
// the nodes along it, the values join across a node line) and the biome is the one of the
// nearest node.
void testInterpolation() {
    const World w(kSeed);
    constexpr double kSpacing = terrain::kCaveEnvironmentSpacing;
    constexpr double kStep = 0.5;
    Rng rng(41);
    const ChunkCoord mountain = streaming_test::mountainChunk(w.heightfield);
    const double mountainX = mountain.x * terrain::kChunkSizeMeters;
    const double mountainZ = mountain.z * terrain::kChunkSizeMeters;
    bool center = true;
    bool nearest = true;
    bool smooth = true;
    bool joined = true;
    double largest = 0.0;
    for (int n = 0; n < 60; ++n) {
        // Half of the points on the flat lands around the origin, half on the steep ground of a
        // mountain.
        const double x = (n % 2 == 0 ? 0.0 : mountainX) + rng.range(-800.0, 800.0);
        const double z = (n % 2 == 0 ? 0.0 : mountainZ) + rng.range(-800.0, 800.0);
        const auto i = static_cast<std::int64_t>(std::floor(x / kSpacing));
        const auto k = static_cast<std::int64_t>(std::floor(z / kSpacing));
        const std::array<CaveEnvironment, 4> nodes = {w.field.node(i, k), w.field.node(i + 1, k),
                                                      w.field.node(i, k + 1),
                                                      w.field.node(i + 1, k + 1)};
        const CaveEnvironment middle = w.field.at((static_cast<double>(i) + 0.5) * kSpacing,
                                                  (static_cast<double>(k) + 0.5) * kSpacing);
        const float meanT = (nodes[0].temperature + nodes[1].temperature + nodes[2].temperature +
                             nodes[3].temperature) /
                            4.0f;
        const float meanH =
            (nodes[0].humidity + nodes[1].humidity + nodes[2].humidity + nodes[3].humidity) / 4.0f;
        center = center && std::abs(middle.temperature - meanT) < 1.0e-3f &&
                 std::abs(middle.humidity - meanH) < 1.0e-4f;
        const double fx = x / kSpacing - static_cast<double>(i);
        const double fz = z / kSpacing - static_cast<double>(k);
        const std::size_t closest = (fx < 0.5 ? 0U : 1U) + (fz < 0.5 ? 0U : 2U);
        nearest = nearest && w.field.at(x, z).biome == nodes[closest].biome;

        const CaveEnvironment here = w.field.at(x, z);
        const Slope along[2] = {slopeAround(w.field, i, k, true),
                                slopeAround(w.field, i, k, false)};
        for (int axis = 0; axis < 2; ++axis) {
            const CaveEnvironment moved =
                w.field.at(x + (axis == 0 ? kStep : 0.0), z + (axis == 1 ? kStep : 0.0));
            const double dT = std::abs(static_cast<double>(moved.temperature - here.temperature));
            const double dH = std::abs(static_cast<double>(moved.humidity - here.humidity));
            smooth = smooth && dT <= kStep / kSpacing * along[axis].temperature + 1.0e-3 &&
                     dH <= kStep / kSpacing * along[axis].humidity + 1.0e-4;
            largest = std::max(largest, dT);
        }
        const double line = (static_cast<double>(i) + 1.0) * kSpacing;
        const CaveEnvironment before = w.field.at(line - 1.0e-6, z);
        const CaveEnvironment after = w.field.at(line + 1.0e-6, z);
        joined = joined && std::abs(before.temperature - after.temperature) < 1.0e-3f &&
                 std::abs(before.humidity - after.humidity) < 1.0e-4f;
    }
    std::printf("environment: the temperature moves by at most %.3f C over %.1f m\n", largest,
                kStep);
    TEST_CHECK(center && nearest && smooth && joined);
}

// The cache gives the values of the field, on first use and after.
void testCache() {
    const World w(kSeed);
    CaveEnvironmentCache cache(w.field);
    Rng rng(43);
    bool same = true;
    for (int n = 0; n < 60; ++n) {
        const double x = rng.range(-300.0, 300.0);
        const double z = rng.range(-300.0, 300.0);
        const CaveEnvironment expected = w.field.at(x, z);
        same = same && cache.at(x, z) == expected && cache.at(x, z) == expected;
    }
    TEST_CHECK(same);
}

// ---------------------------------------------------------------------------

const terrain::CaveParams kCaves{};

// LOD 0 to 2 tiles at the entrances: a material per vertex of the terrain and of the lakes, the
// outer surface painted as such and nothing deep painted as it, at least three kinds of cave
// wall, and no speleothem or lake where the LOD does not carve them.
void testTileMaterials(const TerrainTiles& tiles, const std::vector<ChunkCoord>& chunks) {
    std::set<CaveMaterial> kinds;
    bool sized = true;
    bool surface = true;
    bool waterKinds = true;
    bool farLods = true;
    std::size_t tilesWithSpeleothems = 0;
    for (const ChunkCoord chunk : chunks) {
        for (int lod = 0; lod < 3; ++lod) {
            const TileData data = tiles.generate(requestOf(chunk, lod));
            const SurfaceCounts counts = countSurface(tiles, data);
            sized = sized && !data.mesh.vertices.empty() &&
                    data.materials.size() == data.mesh.vertices.size() &&
                    data.waterMaterials.size() == data.water.vertices.size();
            surface = surface && counts.outerPainted == counts.outer && counts.deepPainted == 0;
            waterKinds = waterKinds && onlyWaterOrIce(data);
            farLods = farLods && (lod == 0 || data.speleothems.empty()) &&
                      (lod < 2 || (data.water.vertices.empty() && data.waterMaterials.empty()));
            tilesWithSpeleothems += data.speleothems.empty() ? 0 : 1;
            std::set<CaveMaterial> here(data.materials.begin(), data.materials.end());
            std::printf("LOD %d tile at (%d, %d): %zu vertices, %zu outer (%zu painted), %zu deep "
                        "(%zu painted as surface), %zu kinds, %zu lake vertices, %zu "
                        "speleothems\n",
                        lod, chunk.x, chunk.z, counts.vertices, counts.outer, counts.outerPainted,
                        counts.deep, counts.deepPainted, here.size(), data.water.vertices.size(),
                        data.speleothems.size());
            here.erase(CaveMaterial::Surface);
            kinds.insert(here.begin(), here.end());
        }
    }
    std::string names;
    for (const CaveMaterial kind : kinds) {
        names += std::string(names.empty() ? "" : ", ") + terrain::caveMaterialName(kind);
    }
    std::printf("materials of the caves: %s\n", names.c_str());
    TEST_CHECK(sized && surface && waterKinds && farLods);
    TEST_CHECK(kinds.size() >= 3);
}

// Generation is deterministic (twice, and from another world with the same seed), and the
// materials and the speleothems never change the geometry: the same tile with other thresholds
// of materials and without speleothem keeps its meshes, plants and ground range.
void testGeometryUnchanged(const TerrainTiles& tiles, const std::vector<ChunkCoord>& chunks) {
    terrain::CaveParams other = kCaves;
    other.materials.surfaceDepth = 0.0f;
    other.materials.deepDepth = 1.0f;
    other.materials.floorNormal = -1.0f;
    other.speleothems.chance = 0.0f;
    const TerrainTiles again(kSeed, {}, {}, kCaves);
    const TerrainTiles recolored(kSeed, {}, {}, other);
    bool deterministic = true;
    bool geometry = true;
    bool recolor = true;
    bool bare = true;
    std::size_t speleothems = 0;
    for (const ChunkCoord chunk : chunks) {
        for (int lod = 0; lod < 2; ++lod) {
            const terrain::TileRequest request = requestOf(chunk, lod);
            const TileData data = tiles.generate(request);
            const TileData changed = recolored.generate(request);
            deterministic =
                deterministic && data == tiles.generate(request) && data == again.generate(request);
            geometry = geometry && data.mesh == changed.mesh && data.water == changed.water &&
                       data.plants == changed.plants && data.originX == changed.originX &&
                       data.originY == changed.originY && data.originZ == changed.originZ &&
                       data.minHeight == changed.minHeight && data.maxHeight == changed.maxHeight;
            recolor = recolor && data.materials != changed.materials;
            bare = bare && changed.speleothems.empty();
            speleothems += data.speleothems.size();
        }
    }
    std::printf("geometry: %zu speleothems over the LOD 0 tiles\n", speleothems);
    TEST_CHECK(deterministic && geometry && recolor && bare);
    TEST_CHECK(speleothems > 0);
}

// Without caves a tile has no materials, lakes or speleothems; with caves, the far LOD 3 has
// none either and is the tile of the world without caves, with or without seams.
void testWithoutCaves(const TerrainTiles& tiles, const std::vector<ChunkCoord>& chunks) {
    const TerrainTiles plain(kSeed);
    bool empty = true;
    bool far = true;
    for (const ChunkCoord chunk : chunks) {
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            const TileData bare = plain.generate(requestOf(chunk, lod));
            empty = empty && bare.materials.empty() && bare.water.vertices.empty() &&
                    bare.waterMaterials.empty() && bare.speleothems.empty();
        }
        for (const unsigned seams : {0U, terrain::kSeamNegX | terrain::kSeamPosZ}) {
            const terrain::TileRequest request{terrain::tileContaining(chunk, terrain::kMaxLod),
                                               seams};
            const TileData data = tiles.generate(request);
            far = far && data.materials.empty() && data.water.vertices.empty() &&
                  data.waterMaterials.empty() && data.speleothems.empty() &&
                  data == plain.generate(request);
        }
    }
    TEST_CHECK(empty);
    TEST_CHECK(far);
}

// ---------------------------------------------------------------------------

// A LOD 0 tile over a lake: every vertex of the water lies under the ground and in the cave air
// (within one cell of density >= 0), in a wet aquifer cell, with water or ice by whether the
// cell is frozen; the LOD 1 tile of the same chunk is consistent and the LOD 2 and 3 ones have no
// lake.
void testLakes(const TerrainTiles& tiles) {
    const std::optional<Lake> lake = findLake(tiles);
    TEST_CHECK(lake.has_value());
    if (!lake) {
        return;
    }
    const TileData data = tiles.generate(requestOf(lake->chunk, 0));
    const LakeChecks checks = checkLake(tiles, data);
    const TileData coarse = tiles.generate(requestOf(lake->chunk, 1));
    std::printf("lake: chunk (%d, %d) with %zu samples of water, %zu water vertices (%zu under the "
                "ground, %zu in the air, %zu in wet cells, %zu with the right material), %zu "
                "speleothems; LOD 1: %zu water vertices\n",
                lake->chunk.x, lake->chunk.z, lake->samples, checks.vertices, checks.belowGround,
                checks.inAir, checks.sameCell, checks.iceMatches, data.speleothems.size(),
                coarse.water.vertices.size());
    TEST_CHECK(checks.vertices >= 20);
    TEST_CHECK(checks.belowGround == checks.vertices && checks.inAir == checks.vertices);
    TEST_CHECK(checks.sameCell == checks.vertices && checks.iceMatches == checks.vertices);
    TEST_CHECK(data.water.normals.size() == data.water.vertices.size());
    TEST_CHECK(data.water.indices.size() % 3 == 0 && onlyWaterOrIce(data));
    TEST_CHECK(speleothemsMatch(tiles, data));
    TEST_CHECK(coarse.waterMaterials.size() == coarse.water.vertices.size() &&
               onlyWaterOrIce(coarse));
    for (int lod = 2; lod < terrain::kLodCount; ++lod) {
        const TileData far = tiles.generate(requestOf(lake->chunk, lod));
        TEST_CHECK(far.water.vertices.empty() && far.water.indices.empty());
        TEST_CHECK(far.waterMaterials.empty() && far.speleothems.empty());
    }
}

} // namespace

int main() {
    testSurfaceIceAndMudRules();
    testRockRules();
    testParameters();
    testNames();
    testNodes();
    testInterpolation();
    testCache();
    const TerrainTiles tiles(kSeed, {}, {}, kCaves);
    const std::vector<CaveEntrance> entrances = streaming_test::entrancesNearOrigin(tiles);
    TEST_CHECK(entrances.size() >= 2);
    if (entrances.size() < 2) {
        return test::result();
    }
    const std::vector<ChunkCoord> chunks = entranceChunks(entrances);
    testTileMaterials(tiles, chunks);
    testGeometryUnchanged(tiles, chunks);
    testWithoutCaves(tiles, chunks);
    testLakes(tiles);
    return test::result();
}
