// The surface blend of the samples of the far horizon (HorizonCell::blend, agent 29):
// - below HorizonParams::blendFromLevel, the blend of the point; on level 0 the texels of the
//   coarsest tiles (TileData::surfaceBlend of LOD 3) at the same points, bit for bit, without and
//   with the planet level (its climate, through its hydrology); the same point blend on the
//   levels 1 to 3 at the points they share with level 0; its slot 0 the biome and the material;
// - from that level on, the mean over the footprint of the point blends of the 3 x 3 samples
//   around (horizon::footprintBlend), equal to the point where they all agree;
// - weights summing to 255 on every level, the same bits whatever the rectangle and the thread;
// - the edits that decide the material give it all the weight of the materials;
// - dominantBlend: the heaviest first, the lower id between equal weights, sums of 1.
#include "horizon_blend.hpp"

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "horizon_test_support.hpp"
#include "planet_golden_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {

using terrain::BlendTexel;
using terrain::HorizonCell;
using terrain::HorizonParams;
using terrain::HorizonRect;
using terrain::HorizonSampler;

// A border of the tropical forest and the savanna in the default world (without the planet).
constexpr double kBorderX = -26'927'500.0;
constexpr double kBorderZ = -270'336.0;

int threadCount() {
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

// The sample of a level at a point of its lattice.
HorizonCell cellAt(const HorizonSampler& sampler, int level, double x, double z) {
    const int pitch = terrain::levelPitch(sampler.params(), level);
    const auto i = static_cast<std::int64_t>(std::floor(x / pitch));
    const auto k = static_cast<std::int64_t>(std::floor(z / pitch));
    return sampler.sample(level, {i, k, i + 1, k + 1}).front();
}

bool sumsTo255(BlendTexel texel) {
    const std::uint32_t b = texel.biomeWeights;
    const std::uint32_t m = texel.materialWeights;
    return (b & 255U) + ((b >> 8U) & 255U) <= 255U && (m & 255U) + ((m >> 8U) & 255U) <= 255U;
}

// Level 0 against the texels of the LOD 3 tiles around the chunk, and the levels 1 to 3 at the
// points they share with it; returns the texels compared that blend more than one biome.
int compareWithTiles(const terrain::TerrainTiles& tiles, const HorizonParams& params,
                     terrain::ChunkCoord chunk) {
    const HorizonSampler sampler(tiles.heightfield(), tiles.ecology(), params);
    const terrain::TileData data = tiles.generate({terrain::tileContaining(chunk, 3), 0});
    const double pitch = terrain::kTileBlendStride * terrain::tileCellMeters(3);
    const int level0 = terrain::levelPitch(params, 0);
    const HorizonRect rect{static_cast<std::int64_t>(data.originX / level0),
                           static_cast<std::int64_t>(data.originZ / level0),
                           static_cast<std::int64_t>(data.originX / level0) + 4 * 32 + 1,
                           static_cast<std::int64_t>(data.originZ / level0) + 4 * 32 + 1};
    const std::vector<HorizonCell> cells = sampler.sample(0, rect);
    int mixed = 0;
    for (int k = 0; k < terrain::kTileBlendTexels; ++k) {
        for (int i = 0; i < terrain::kTileBlendTexels; ++i) {
            const BlendTexel texel =
                data.surfaceBlend[static_cast<std::size_t>(k * terrain::kTileBlendTexels + i)];
            const auto at = static_cast<std::size_t>(4 * k * rect.width() + 4 * i);
            TEST_CHECK(cells[at].blend == texel);
            mixed += terrain::unpackBlend(texel).biomeWeights[1] > 0.0f ? 1 : 0;
            if ((i + k) % 8 == 0) {
                const double x = data.originX + i * pitch;
                const double z = data.originZ + k * pitch;
                for (int level = 1; level < 4; ++level) {
                    const int step = terrain::levelPitch(params, level);
                    if (std::fmod(x, step) == 0.0 && std::fmod(z, step) == 0.0) {
                        TEST_CHECK(cellAt(sampler, level, x, z).blend == texel);
                    }
                }
            }
        }
    }
    return mixed;
}

void testTilesDefaultWorld() {
    const terrain::TerrainTiles tiles(streaming_test::kSeed);
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(tiles.heightfield());
    const auto chunk = [](double x, double z) {
        return terrain::ChunkCoord{static_cast<int>(std::floor(x / terrain::kChunkSizeMeters)),
                                   static_cast<int>(std::floor(z / terrain::kChunkSizeMeters))};
    };
    const int mixed = compareWithTiles(tiles, HorizonParams{}, mountain) +
                      compareWithTiles(tiles, HorizonParams{}, chunk(kBorderX, kBorderZ));
    TEST_CHECK(mixed > 0);
    std::printf("level 0 = LOD 3 texels around the mountains and the border: %d texels of several "
                "biomes\n",
                mixed);
}

void testTilesWithPlanet() {
    const terrain::Heightfield relief(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      golden::kSmallPlanet);
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threadCount())),
        golden::smallPlanetParams(threadCount()));
    auto hydrology =
        std::make_shared<const terrain::Hydrology>(terrain::kDefaultWorldSeed, relief, planet);
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed, {}, {}, std::nullopt,
                                      golden::kSmallPlanet, hydrology);
    HorizonParams params;
    params.levels = 7;
    params.world = golden::kSmallPlanet;
    int mixed = 0;
    for (const terrain::ChunkCoord chunk :
         {terrain::ChunkCoord{40, 12}, terrain::ChunkCoord{-200, 90},
          terrain::ChunkCoord{700, -300}}) {
        mixed += compareWithTiles(tiles, params, chunk);
    }
    TEST_CHECK(mixed > 0);
    std::printf("level 0 = LOD 3 texels on the small planet: %d texels of several biomes\n", mixed);
}

// Slot 0 of the point blends, the sums of 255 on every level, and the footprint means.
void testLevels() {
    const horizon_test::World world;
    HorizonParams params = horizon_test::smallParams();
    HorizonParams points = params;
    points.blendFromLevel = terrain::kHorizonMaxLevels;
    const HorizonSampler sampler(world.field, world.ecology, params);
    const HorizonSampler pointSampler(world.field, world.ecology, points);
    int differing = 0;
    int uniform = 0;
    int compared = 0;
    for (int level = 0; level < params.levels; ++level) {
        const terrain::HorizonIndex c = terrain::canonicalCenter(params, level, kBorderX, kBorderZ);
        const HorizonRect rect{c.x - 6, c.z - 5, c.x + 6, c.z + 5};
        const HorizonRect wide{rect.x0 - 1, rect.z0 - 1, rect.x1 + 1, rect.z1 + 1};
        const std::vector<HorizonCell> cells = sampler.sample(level, rect);
        const std::vector<HorizonCell> around = pointSampler.sample(level, wide);
        std::size_t n = 0;
        for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
            for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
                const HorizonCell& cell = cells[n];
                const auto at = [&](std::int64_t di, std::int64_t dk) {
                    return static_cast<std::size_t>((k - wide.z0 + dk) * wide.width() +
                                                    (i - wide.x0 + di));
                };
                TEST_CHECK(sumsTo255(cell.blend));
                // Everything but the blend is the same with point blends.
                const HorizonCell& point = around[at(0, 0)];
                TEST_CHECK(point.biome == cell.biome && point.material == cell.material);
                if (level < params.blendFromLevel) {
                    TEST_CHECK(point.blend == cell.blend);
                    const terrain::SurfaceBlend blend = terrain::unpackBlend(cell.blend);
                    TEST_CHECK(blend.biomes[0] == cell.biome);
                    TEST_CHECK(blend.materials[0] == cell.material);
                    continue;
                }
                bool same = true;
                terrain::BlendWeights mean;
                for (std::size_t m = 0; m < 9; ++m) {
                    const BlendTexel t = around[at(static_cast<std::int64_t>(m % 3) - 1,
                                                   static_cast<std::int64_t>(m / 3) - 1)]
                                             .blend;
                    same = same && t == point.blend;
                    terrain::addTexel(mean, t, terrain::horizon::kFootprintShares[m]);
                }
                if (same) {
                    TEST_CHECK(cell.blend == point.blend);
                    ++uniform;
                }
                // The mean of the 9 texels: the weights of the sample within the rounding of the
                // texels (1/255 each), where at most 3 ids share the weights.
                const auto ids = [](const auto& weights) {
                    return std::count_if(weights.begin(), weights.end(),
                                         [](float w) { return w > 0.0f; });
                };
                if (ids(mean.biomes) <= 3 && ids(mean.materials) <= 3) {
                    terrain::BlendWeights own;
                    terrain::addTexel(own, cell.blend, 1.0f);
                    for (std::size_t b = 0; b < own.biomes.size(); ++b) {
                        TEST_CHECK(std::abs(own.biomes[b] - mean.biomes[b]) <= 2.0f / 255.0f);
                    }
                    for (std::size_t m = 0; m < own.materials.size(); ++m) {
                        TEST_CHECK(std::abs(own.materials[m] - mean.materials[m]) <= 2.0f / 255.0f);
                    }
                    ++compared;
                }
                differing += cell.blend == point.blend ? 0 : 1;
            }
        }
    }
    TEST_CHECK(differing > 0 && uniform > 0 && compared > 0);
    std::printf("footprint means: %d samples differ from their point, %d uniform, %d compared "
                "with the mean of the texels\n",
                differing, uniform, compared);
}

// The same samples whatever the rectangle and the thread computing them.
void testRectanglesAndThreads() {
    const horizon_test::World world;
    const HorizonParams params = horizon_test::smallParams();
    const HorizonSampler sampler(world.field, world.ecology, params);
    for (const int level : {0, 5}) {
        const terrain::HorizonIndex c =
            terrain::canonicalCenter(params, level, horizon_test::kX, horizon_test::kZ);
        const HorizonRect rect{c.x - 8, c.z - 8, c.x + 8, c.z + 8};
        const std::vector<HorizonCell> whole = sampler.sample(level, rect);
        std::vector<HorizonCell> rows(whole.size());
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&, t] {
                const HorizonRect part{rect.x0, rect.z0 + 4 * t, rect.x1, rect.z0 + 4 * t + 4};
                const std::vector<HorizonCell> cells = sampler.sample(level, part);
                std::copy(cells.begin(), cells.end(),
                          rows.begin() + static_cast<std::ptrdiff_t>(cells.size()) * t);
            });
        }
        for (std::thread& worker : workers) {
            worker.join();
        }
        TEST_CHECK(rows == whole);
    }
}

// An edit that decides the material: all the weight of the materials on it, the biomes kept.
void testEdits() {
    const horizon_test::World world;
    const HorizonParams params = horizon_test::smallParams();
    const terrain::EditSummarySource edits = [](const terrain::CellFootprint&) {
        return std::optional<terrain::EditSummary>(
            terrain::EditSummary{1.0f, terrain::SurfaceMaterial::Rock, std::nullopt});
    };
    const HorizonSampler plain(world.field, world.ecology, params);
    const HorizonSampler edited(world.field, world.ecology, params, edits);
    for (const int level : {0, 6}) {
        const terrain::HorizonIndex c =
            terrain::canonicalCenter(params, level, horizon_test::kX, horizon_test::kZ);
        const HorizonRect rect{c.x - 3, c.z - 3, c.x + 3, c.z + 3};
        const std::vector<HorizonCell> a = plain.sample(level, rect);
        const std::vector<HorizonCell> b = edited.sample(level, rect);
        for (std::size_t n = 0; n < a.size(); ++n) {
            const terrain::SurfaceBlend before = terrain::unpackBlend(a[n].blend);
            const terrain::SurfaceBlend after = terrain::unpackBlend(b[n].blend);
            TEST_CHECK(after.biomes == before.biomes && after.biomeWeights == before.biomeWeights);
            TEST_CHECK(after.materials[0] == terrain::SurfaceMaterial::Rock &&
                       after.materialWeights[0] == 1.0f && after.materialWeights[1] == 0.0f &&
                       after.materialWeights[2] == 0.0f);
            TEST_CHECK(sumsTo255(b[n].blend));
        }
    }
}

void testDominant() {
    using terrain::Biome;
    terrain::BlendWeights w;
    w.biomes[static_cast<std::size_t>(Biome::RockyDesert)] = 0.25f;
    w.biomes[static_cast<std::size_t>(Biome::Ocean)] = 0.25f;
    w.biomes[static_cast<std::size_t>(Biome::Mountain)] = 0.5f;
    w.biomes[static_cast<std::size_t>(Biome::Tundra)] = 0.125f;
    w.materials[3] = 1.0f;
    const terrain::SurfaceBlend blend = terrain::horizon::dominantBlend(w);
    TEST_CHECK(blend.biomes[0] == Biome::Mountain);
    // Equal weights: the lower id first (the ocean is 0).
    TEST_CHECK(blend.biomes[1] == Biome::Ocean && blend.biomes[2] == Biome::RockyDesert);
    TEST_CHECK(blend.biomeWeights[0] == 0.5f / 1.0f && blend.biomeWeights[1] == 0.25f);
    TEST_CHECK(blend.materials[0] == static_cast<terrain::SurfaceMaterial>(3));
    TEST_CHECK(blend.materials[1] == blend.materials[0] && blend.materialWeights[1] == 0.0f);
    TEST_CHECK(blend.materialWeights[0] == 1.0f);
}

} // namespace

int main() {
    testDominant();
    testRectanglesAndThreads();
    testEdits();
    testTilesDefaultWorld();
    testTilesWithPlanet();
    testLevels();
    return test::result();
}
