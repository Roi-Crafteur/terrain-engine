// The paths of the relief compared bit for bit (agent 55, task T3 of the audit of agent 53), on
// the default world with its planet file as Unreal loads it (coast_planet_support.hpp). Several
// copies of the chain of sampling exist (sampleAt, the batches through waterOf, seenAt), and
// each consumer reads its own: a step missing from one copy is a world that changes on one path
// only (the dunes once vanished from the horizon that way, and no test saw it).
//
// About 2,000 points in strata (land, coast band, erg, river corridor, wall, dry depression,
// sea), at the footprints 0, 1, 2, 4, 8, 32, 128 and 512 m, against forFootprint(f).sampleAt,
// every field of HeightSample:
// - samplesAt, samplesAt with the rock, heightsAt: batches of two points 1 m apart (the water of
//   the batch through the region of its cell, as in a tile);
// - seenAt: out of the band of the coasts, every field; in it, all but those its faces, ramps
//   and spires move (height, water, waterLevel, rock); seenHeightsAt = seenAt height everywhere;
// - a cell of the far horizon (HorizonSampler, no crest lift) on the levels of 8, 32, 128 and
//   512 m: seenAt and its site, classified as the horizon does (horizon::pointSite);
// - the LOD 3 tiles (forFootprint(8)): their columns and water = sampleAt, their texels = the
//   cells of level 0 of the horizon.
// The test prints a table stage x stratum and names the first stage that differs.
#include "relief_paths_support.hpp"

#include "horizon_blend.hpp"
#include "tile_lattice.hpp"

#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

// Every field of HeightSample is compared (relief_paths_support.hpp, kFloatFields): a change of
// the structure must be read there first. The same size is pinned by agent 46.
static_assert(sizeof(terrain::HeightSample) == 72,
              "HeightSample changed: compare the new field in relief_paths_test (kFloatFields)");

namespace {

using namespace relief_paths;
using terrain::Heightfield;
using terrain::HeightSample;
using terrain::HorizonCell;
using terrain::WaterKind;

constexpr std::array<double, 8> kFootprints = {0.0, 1.0, 2.0, 4.0, 8.0, 32.0, 128.0, 512.0};
// The horizon on one point in 2 of each stratum, a LOD 3 tile around one in 8.
constexpr std::size_t kHorizonEvery = 2;
constexpr std::size_t kTileEvery = 8;

std::optional<FieldDiff> heightDiff(const char* name, float expected, float read) {
    return same(expected, read) ? std::nullopt
                                : std::optional<FieldDiff>{FieldDiff{name, expected, read}};
}

// The point and its neighbor 1 m away along both axes: a batch of two.
struct Pair {
    std::array<double, 2> xs;
    std::array<double, 2> zs;
};

void comparePoint(const Heightfield& level, double f, const Point& p, Tally& tally) {
    const HeightSample ref = level.sampleAt(p.x, p.z);
    const Pair b{{p.x, p.x + 1.0}, {p.z, p.z + 1.0}};
    std::array<HeightSample, 2> out{};
    level.samplesAt(b.xs, b.zs, out);
    tally.add(Stage::SamplesAt, p, f, firstField(ref, out[0]));
    std::array<terrain::RockColumn, 2> rock{};
    level.samplesAt(b.xs, b.zs, out, rock);
    tally.add(Stage::SamplesAtRock, p, f, firstField(ref, out[0]));
    std::array<float, 2> heights{};
    level.heightsAt(b.xs, b.zs, heights);
    tally.add(Stage::HeightsAt, p, f, heightDiff("height", ref.height, heights[0]));
    if (f <= 0.0) {
        return; // no level of the horizon sees the points
    }
    const HeightSample seen = level.seenAt(p.x, p.z, f);
    const std::optional<FieldDiff> diff = firstField(ref, seen, p.inBand);
    tally.add(Stage::SeenAt, p, f, diff);
    if (!diff && p.inBand && firstField(ref, seen)) {
        tally.byDesign(Stage::SeenAt, p);
    }
    level.seenHeightsAt(b.xs, b.zs, heights, f);
    tally.add(Stage::SeenHeightsAt, p, f, heightDiff("height", seen.height, heights[0]));
}

void testPaths(const Heightfield& field, const std::vector<Point>& points, Tally& tally) {
    for (const double f : kFootprints) {
        const Heightfield level = field.forFootprint(f);
        for (const Point& p : points) {
            comparePoint(level, f, p, tally);
        }
    }
}

// ---- The far horizon --------------------------------------------------------------------

// A cell of the horizon as its sampler makes it from seenAt (horizon_sampler.cpp, pointClass and
// classify): the site of the seen sample from its neighbors step meters away, the bare rock of the
// foot of the walls under the sea from the rise over one and two pitches.
HorizonCell expectedCell(const Heightfield& level, const terrain::Ecology& ecology, double x,
                         double z, int pitch, int step) {
    const terrain::WorldSize& world = level.world();
    const auto seenAt = [&](double dx, double dz) {
        return level
            .seenAt(terrain::noise::wrapCanonical(x + dx, world.x),
                    terrain::noise::wrapCanonical(z + dz, world.z), pitch)
            .height;
    };
    const HeightSample seen = level.seenAt(x, z, pitch);
    HeightSample point; // the fields the grid of the sampler keeps
    point.height = seen.height;
    point.continentalness = seen.continentalness;
    point.water = seen.water;
    point.waterLevel = seen.waterLevel;
    point.dunes = seen.dunes;
    point.erg = seen.erg;
    point.ergFloor = seen.ergFloor;
    terrain::SurfaceSite site = terrain::horizon::pointSite(
        point, {seenAt(step, 0), seenAt(-step, 0), seenAt(0, step), seenAt(0, -step)}, step);
    const terrain::ClimateSample climate = ecology.climate().sample(x, z, site);
    float rise = 0.0f;
    for (const auto& [di, dk] : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
        const float near = seenAt(di * pitch, dk * pitch);
        const float far = seenAt(2.0 * di * pitch, 2.0 * dk * pitch);
        rise = std::max({rise, static_cast<float>(static_cast<double>(near - seen.height) / pitch),
                         static_cast<float>(static_cast<double>(far - seen.height) /
                                            (2.0 * pitch))});
    }
    site.bareRock = seen.rock > 0.5f ||
                    (terrain::mayPlunge(site) &&
                     terrain::barePlunge(site, rise, ecology.params().materials.rockSlope));
    const terrain::SurfaceBlend blend = ecology.blendAt(x, z, site, climate);
    HorizonCell cell;
    cell.height = seen.height;
    cell.biome = blend.biomes[0];
    cell.material = blend.materials[0];
    cell.blend = terrain::packBlend(blend);
    cell.flags = static_cast<std::uint8_t>((site.dry() ? 0U : terrain::kHorizonWater) |
                                           (site.water == WaterKind::Ocean ? terrain::kHorizonSea
                                                                           : 0U));
    cell.waterLevel = site.dry() ? terrain::kNoWaterLevel : site.waterLevel;
    return cell;
}

// The first field of the blend texel that differs from the expected one, if any.
std::optional<FieldDiff> blendDiff(const terrain::BlendTexel& expected,
                                   const terrain::BlendTexel& read) {
    using Field = std::uint32_t terrain::BlendTexel::*;
    const std::array<std::pair<const char*, Field>, 4> fields = {{
        {"blend.biomes", &terrain::BlendTexel::biomes},
        {"blend.materials", &terrain::BlendTexel::materials},
        {"blend.biomeWeights", &terrain::BlendTexel::biomeWeights},
        {"blend.materialWeights", &terrain::BlendTexel::materialWeights},
    }};
    for (const auto& [name, field] : fields) {
        if (expected.*field != read.*field) {
            return FieldDiff{name, static_cast<double>(expected.*field),
                             static_cast<double>(read.*field)};
        }
    }
    return std::nullopt;
}

// The first field of the cell that differs from the expected one: the blend only below
// blendFromLevel (above it, the mean over the footprint of the samples around it).
std::optional<FieldDiff> cellDiff(const HorizonCell& expected, const HorizonCell& read,
                                  bool pointBlend) {
    if (auto d = heightDiff("height", expected.height, read.height)) {
        return d;
    }
    if (expected.flags != read.flags) {
        return FieldDiff{"flags", static_cast<double>(expected.flags),
                         static_cast<double>(read.flags)};
    }
    if (auto d = heightDiff("waterLevel", expected.waterLevel, read.waterLevel)) {
        return d;
    }
    if (expected.biome != read.biome) {
        return FieldDiff{"biome", static_cast<double>(expected.biome),
                         static_cast<double>(read.biome)};
    }
    if (expected.material != read.material) {
        return FieldDiff{"material", static_cast<double>(expected.material),
                         static_cast<double>(read.material)};
    }
    return pointBlend ? blendDiff(expected.blend, read.blend) : std::nullopt;
}

void testHorizon(const terrain::TerrainTiles& tiles, const std::vector<Point>& points,
                 Tally& tally) {
    terrain::HorizonParams params;
    params.crestRatio = 0; // the point heights of the ground: seenAt, no lift to the crests
    const terrain::HorizonSampler sampler(tiles.heightfield(), tiles.ecology(), params);
    for (const int level : {0, 2, 4, 6}) {
        const int pitch = terrain::levelPitch(params, level);
        const Heightfield field = tiles.heightfield().forFootprint(pitch);
        const terrain::HorizonIndex counts = terrain::levelCounts(params, level);
        for (const Point& p : points) {
            if (p.rank % kHorizonEvery != 0) {
                continue;
            }
            const terrain::HorizonIndex at = terrain::canonicalSample(
                {static_cast<std::int64_t>(std::floor(p.x / pitch)),
                 static_cast<std::int64_t>(std::floor(p.z / pitch))},
                counts);
            const Point q{terrain::samplePosition(at.x, pitch),
                          terrain::samplePosition(at.z, pitch), p.stratum, p.inBand, p.rank};
            const HorizonCell read = sampler.sample(level, {at.x, at.z, at.x + 1, at.z + 1})[0];
            const HorizonCell expected = expectedCell(field, tiles.ecology(), q.x, q.z, pitch,
                                                      params.materialSlopeStep);
            tally.add(Stage::HorizonCell, q, pitch,
                      cellDiff(expected, read, level < params.blendFromLevel));
        }
    }
}

// ---- The LOD 3 tiles ----------------------------------------------------------------------

constexpr double kTilePitch = 8.0; // the cells of the LOD 3 tiles, the pitch of horizon level 0

// The columns of the LOD 3 tile of the point (tile::sampleColumns, as TerrainTiles::generate
// samples them) and the water columns of the tile it generates, against sampleAt.
void compareColumns(const Heightfield& level, const terrain::TileData& data,
                    const terrain::tile::TileLattice& lattice, const Point& p, Tally& tally) {
    const terrain::tile::TileColumns columns =
        terrain::tile::sampleColumns(level, lattice, true, terrain::kTileBlendStride);
    for (int k = 0; k < terrain::kGridSamples; ++k) {
        for (int i = 0; i < terrain::kGridSamples; ++i) {
            const auto at = static_cast<std::size_t>(k * terrain::kGridSamples + i);
            const Point q{lattice.x0 + static_cast<double>(i) * lattice.spacing,
                          lattice.z0 + static_cast<double>(k) * lattice.spacing, p.stratum,
                          p.inBand, p.rank};
            const HeightSample ref = level.sampleAt(q.x, q.z);
            HeightSample read = ref; // the fields the columns keep, from them
            read.height = columns.heights[at];
            read.continentalness = columns.continentalness[at];
            read.water = columns.waterKinds[at];
            read.waterLevel = read.water == WaterKind::None ? ref.waterLevel
                                                            : columns.waterLevels[at];
            read.dunes = columns.dunes[at];
            read.erg = columns.erg[at];
            read.ergFloor = columns.ergFloors[at];
            read.rock = columns.bareRocks[at];
            tally.add(Stage::Lod3Columns, q, kTilePitch, firstField(ref, read));
            if (data.waterColumns.empty()) {
                continue;
            }
            const terrain::WaterColumn& w = data.waterColumns[at];
            const float level0 =
                ref.water == WaterKind::None ? terrain::kNoWaterLevel : ref.waterLevel;
            std::optional<FieldDiff> d = heightDiff("level", level0, w.level);
            if (w.kind != ref.water) {
                d = FieldDiff{"kind", static_cast<double>(ref.water), static_cast<double>(w.kind)};
            }
            tally.add(Stage::Lod3Water, q, kTilePitch, d);
        }
    }
}

// Whether the faces, ramps and spires of the coasts move the ground of horizon level 0 around
// the texel at (x, z) (its site, its rise): the tiles keep the relief there, by design. Only in
// the band of the coasts, where seenAt may differ from sampleAt.
bool facedAround(const Heightfield& level, double x, double z) {
    if (!level.coastAt(x, z).inBand) {
        return false;
    }
    for (const auto& [dx, dz] : {std::array<double, 2>{0, 0}, {8, 0}, {-8, 0}, {0, 8}, {0, -8},
                                 {16, 0}, {-16, 0}, {0, 16}, {0, -16}}) {
        if (!same(level.sampleAt(x + dx, z + dz).height,
                  level.seenAt(x + dx, z + dz, kTilePitch).height)) {
            return true;
        }
    }
    return false;
}

void compareTexels(const Heightfield& level, const terrain::HorizonSampler& sampler,
                   const terrain::TileData& data, const Point& p, Tally& tally) {
    const auto i0 = static_cast<std::int64_t>(data.originX / kTilePitch);
    const auto k0 = static_cast<std::int64_t>(data.originZ / kTilePitch);
    const std::int64_t side = terrain::kGridSamples;
    const std::vector<HorizonCell> cells = sampler.sample(0, {i0, k0, i0 + side, k0 + side});
    for (int k = 0; k < terrain::kTileBlendTexels; ++k) {
        for (int i = 0; i < terrain::kTileBlendTexels; ++i) {
            const int stride = terrain::kTileBlendStride;
            const Point q{data.originX + i * stride * kTilePitch,
                          data.originZ + k * stride * kTilePitch, p.stratum, p.inBand,
                          p.rank};
            const terrain::BlendTexel texel =
                data.surfaceBlend[static_cast<std::size_t>(k * terrain::kTileBlendTexels + i)];
            const HorizonCell& cell = cells[static_cast<std::size_t>(stride * k * side + stride * i)];
            const std::optional<FieldDiff> diff = blendDiff(cell.blend, texel);
            if (diff && facedAround(level, q.x, q.z)) {
                tally.byDesign(Stage::Lod3Texels, q);
            } else {
                tally.add(Stage::Lod3Texels, q, kTilePitch, diff);
            }
        }
    }
}

void testTiles(const terrain::TerrainTiles& tiles, const std::vector<Point>& points,
               Tally& tally) {
    terrain::HorizonParams params;
    params.crestRatio = 0;
    const terrain::HorizonSampler sampler(tiles.heightfield(), tiles.ecology(), params);
    const Heightfield level = tiles.heightfield().forFootprint(kTilePitch);
    for (const Point& p : points) {
        if (p.rank % kTileEvery != 0) {
            continue;
        }
        const terrain::ChunkCoord chunk{
            static_cast<int>(std::floor(p.x / terrain::kChunkSizeMeters)),
            static_cast<int>(std::floor(p.z / terrain::kChunkSizeMeters))};
        const terrain::TileKey key =
            terrain::wrapTile(terrain::tileContaining(chunk, terrain::kMaxLod), tiles.world());
        const terrain::TileData data = tiles.generate({key, 0});
        compareColumns(level, data, terrain::tile::latticeOf(key), p, tally);
        compareTexels(level, sampler, data, p, tally);
    }
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

int main() {
    const terrain::TerrainTiles& tiles = coast_planet::tiles();
    auto start = std::chrono::steady_clock::now();
    const std::vector<Point> points = drawPoints(tiles.heightfield());
    std::printf("%zu points drawn in %.1f s\n", points.size(), secondsSince(start));
    Tally tally;
    start = std::chrono::steady_clock::now();
    testPaths(tiles.heightfield(), points, tally);
    std::printf("the paths at %zu footprints in %.1f s\n", kFootprints.size(), secondsSince(start));
    start = std::chrono::steady_clock::now();
    testHorizon(tiles, points, tally);
    std::printf("the horizon in %.1f s\n", secondsSince(start));
    start = std::chrono::steady_clock::now();
    testTiles(tiles, points, tally);
    std::printf("the LOD 3 tiles in %.1f s\n", secondsSince(start));
    tally.report();
    TEST_CHECK(!tally.first().has_value());
    for (std::size_t st = 0; st < kStages; ++st) {
        TEST_CHECK(tally.compared(static_cast<Stage>(st)) > 0);
    }
    return test::result();
}
