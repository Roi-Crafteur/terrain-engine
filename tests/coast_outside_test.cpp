// The world out of the zone of the coastal relief (agent 33, coast_zone_support.hpp) keeps every
// bit: checksums of samples, tiles and horizon cells drawn out of the zone, with margins for what
// each of them reads around it, pinned on the code of main before the coastal relief (cb64153).
// On the default world (no planet level: no rivers) and on the small world with its planet level
// and its rivers. The coastal relief must leave every one of them unchanged, on every commit.
#include "terrain/chunk_coord.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/planet/planet_build.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "coast_zone_support.hpp"
#include "golden_support.hpp"
#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace {

using coast_zone::Draws;
using terrain::Heightfield;
using terrain::HeightSample;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// The coast signal changes by up to about 40 meters per meter (the detail and fine coasts): a
// square read every step meters keeps out of the zone between its points with this many meters
// of signal per meter of step, never less than kLeastMargin.
constexpr double kSignalPerMeter = 40.0;
constexpr double kLeastMargin = 1'000.0;
// The places beside the zone: their center within kEdge meters of signal of it.
constexpr double kEdge = 15'000.0;

enum class Side { Land, InnerEdge, Sea, OuterEdge };
constexpr Side kSides[] = {Side::Land, Side::InnerEdge, Side::Sea, Side::OuterEdge};

bool ofSide(Side side, double c) {
    switch (side) {
    case Side::Land:
        return c >= coast_zone::kBandLand + kEdge;
    case Side::InnerEdge:
        return c >= coast_zone::kBandLand && c < coast_zone::kBandLand + kEdge;
    case Side::Sea:
        return c <= -coast_zone::kBandSea - kEdge;
    case Side::OuterEdge:
        return c <= -coast_zone::kBandSea && c > -coast_zone::kBandSea - kEdge;
    }
    return false;
}

struct Place {
    double x = 0.0;
    double z = 0.0;
};

// What a sample reads around it: the square from (x, z) - before to (x, z) + after, read every
// step meters.
struct Reach {
    double before = 0.0;
    double after = 0.0;
    double step = 8.0;
};

bool reachOutside(const Heightfield& field, const Place& p, const Reach& r) {
    const double margin = std::max(kLeastMargin, kSignalPerMeter * r.step);
    return coast_zone::squareOutside(field, p.x - r.before, p.z - r.before, r.before + r.after,
                                     r.step, margin);
}

// count places of a side of the zone drawn over the world, snapped by snap (to the origin of a
// tile, to the center of a horizon window), whose reach is out of the zone.
std::vector<Place> drawPlaces(const Heightfield& field, Draws& draws, Side side, int count,
                              const std::function<Place(Place)>& snap, const Reach& reach) {
    std::vector<Place> places;
    const terrain::WorldSize& w = field.world();
    for (int tries = 0; tries < 400'000 && static_cast<int>(places.size()) < count; ++tries) {
        const Place drawn{(draws.next() - 0.5) * w.x, (draws.next() - 0.5) * w.z};
        if (!ofSide(side, coast_zone::coastSignal(field, drawn.x, drawn.z))) {
            continue;
        }
        const Place p = snap(drawn);
        if (reachOutside(field, p, reach)) {
            places.push_back(p);
        }
    }
    TEST_CHECK(static_cast<int>(places.size()) == count);
    return places;
}

Place identity(Place p) {
    return p;
}

void addSample(golden::Checksum& sum, const HeightSample& s) {
    for (const float v : {s.continentalness, s.macroContinentalness, s.plateContinentalness,
                          s.plateCollision, s.mountainMask, s.relief, s.base, s.mountain, s.detail,
                          s.height, s.waterLevel}) {
        sum.add(v);
    }
    sum.add(static_cast<std::uint64_t>(s.water), 1);
}

// Patches of 16 x 16 points 2 m apart, in one batch each (samplesAt) and the first row point by
// point (sampleAt).
constexpr int kPatchSide = 16;
constexpr double kPatchStep = 2.0;
constexpr Reach kPatchReach{8.0, 8.0 + kPatchSide * kPatchStep, 8.0};

void addPatch(golden::Checksum& sum, const Heightfield& field, const Place& p) {
    std::vector<double> xs;
    std::vector<double> zs;
    for (int j = 0; j < kPatchSide; ++j) {
        for (int i = 0; i < kPatchSide; ++i) {
            xs.push_back(p.x + i * kPatchStep);
            zs.push_back(p.z + j * kPatchStep);
        }
    }
    std::vector<HeightSample> out(xs.size());
    field.samplesAt(xs, zs, out);
    for (const HeightSample& s : out) {
        addSample(sum, s);
    }
    for (int i = 0; i < kPatchSide; ++i) {
        addSample(sum, field.sampleAt(xs[static_cast<std::size_t>(i)], zs[0]));
    }
}

// Everything a tile holds, its surface blend and its water levels too.
void addFullTile(golden::Checksum& sum, const terrain::TileData& data) {
    golden::addTile(sum, data);
    sum.add(static_cast<std::uint64_t>(data.surfaceBlend.size()));
    for (const terrain::BlendTexel& t : data.surfaceBlend) {
        sum.add(static_cast<std::uint64_t>(t.ids), 4);
        sum.add(static_cast<std::uint64_t>(t.weights), 4);
    }
    sum.add(static_cast<std::uint64_t>(data.waterLevels.size()));
    for (const float level : data.waterLevels) {
        sum.add(level);
    }
}

// The tiles of LOD 0 to maxLod holding the chunk of a place, without seams and with two of them.
// A tile reads its columns, one cell beyond for its blend texels, and 8 m around for its plants.
terrain::TileKey keyOf(const Place& p, int lod) {
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(p.x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(p.z / terrain::kChunkSizeMeters))};
    return terrain::tileContaining(chunk, lod);
}

Reach tileReach(int lod) {
    const double cell = terrain::tileCellMeters(lod);
    return {cell + 8.0, cell + 8.0 + terrain::tileSizeMeters(lod), std::min(8.0, cell)};
}

void addTiles(golden::Checksum& sum, const terrain::TerrainTiles& tiles, const Place& p,
              int lod) {
    const terrain::TileKey key = keyOf(p, lod);
    addFullTile(sum, tiles.generate({key, 0}));
    addFullTile(sum, tiles.generate({key, terrain::kSeamPosX | terrain::kSeamNegXNegZ}));
}

// The horizon cells of a rectangle of kCells x kCells samples of a level around a place: they read
// two pitches around them (slopes, crests, detail normals).
constexpr int kCells = 8;
constexpr int kEdgeLevels = 5;

Reach horizonReach(const terrain::HorizonParams& params, int level) {
    const auto pitch = static_cast<double>(terrain::levelPitch(params, level));
    const double half = (kCells / 2 + 2) * pitch;
    return {half, half, std::max(8.0, pitch / 8.0)};
}

void addHorizon(golden::Checksum& sum, const terrain::HorizonSampler& sampler, const Place& p,
                int level) {
    const terrain::HorizonIndex c =
        terrain::canonicalCenter(sampler.params(), level, p.x, p.z);
    const terrain::HorizonRect rect{c.x - kCells / 2, c.z - kCells / 2, c.x + kCells / 2,
                                    c.z + kCells / 2};
    for (const terrain::HorizonCell& cell : sampler.sample(level, rect)) {
        sum.add(cell.height);
        sum.add(cell.gradX);
        sum.add(cell.gradZ);
        sum.add(static_cast<std::uint64_t>(cell.material), 1);
        sum.add(static_cast<std::uint64_t>(cell.biome), 1);
        sum.add(static_cast<std::uint64_t>(cell.flags), 1);
        sum.add(cell.waterLevel);
    }
}

// The place snapped to the center of the horizon window of a level that addHorizon reads.
std::function<Place(Place)> horizonSnap(const terrain::HorizonParams& params, int level) {
    return [&params, level](Place p) {
        const terrain::HorizonIndex c = terrain::canonicalCenter(params, level, p.x, p.z);
        const int pitch = terrain::levelPitch(params, level);
        return Place{terrain::samplePosition(c.x, pitch), terrain::samplePosition(c.z, pitch)};
    };
}

// The tile origin of a place at a LOD, as the reach of its tiles starts there.
std::function<Place(Place)> tileSnap(int lod) {
    return [lod](Place p) -> Place {
        const terrain::TileKey key = keyOf(p, lod);
        return Place{key.origin.x * terrain::kChunkSizeMeters,
                     key.origin.z * terrain::kChunkSizeMeters};
    };
}

struct Sums {
    golden::Checksum patches;
    golden::Checksum tiles;
    golden::Checksum horizon;
};

// The patches, tiles and horizon cells (levels below horizonLevels) of places drawn on each side
// of the zone.
Sums worldSums(const char* name, const terrain::TerrainTiles& tiles, std::uint64_t drawSeed,
               int horizonLevels) {
    const Heightfield& field = tiles.heightfield();
    terrain::HorizonParams params;
    params.levels = horizonLevels;
    params.cellsPerSide = 64;
    params.world = field.world();
    const terrain::HorizonSampler sampler(field, tiles.ecology(), params);
    Draws draws(drawSeed);
    Sums sums;
    int patches = 0;
    int tileCount = 0;
    int windows = 0;
    for (const Side side : kSides) {
        for (const Place& p : drawPlaces(field, draws, side, 8, identity, kPatchReach)) {
            addPatch(sums.patches, field, p);
            ++patches;
        }
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            const Reach reach = tileReach(lod);
            for (const Place& p : drawPlaces(field, draws, side, 4, tileSnap(lod), reach)) {
                addTiles(sums.tiles, tiles, p, lod);
                ++tileCount;
            }
        }
        // Beside the zone, the levels whose windows fit there.
        const bool edge = side == Side::InnerEdge || side == Side::OuterEdge;
        for (int level = 0; level < (edge ? kEdgeLevels : horizonLevels); ++level) {
            for (const Place& p : drawPlaces(field, draws, side, 2, horizonSnap(params, level),
                                             horizonReach(params, level))) {
                addHorizon(sums.horizon, sampler, p, level);
                ++windows;
            }
        }
    }
    std::printf("%s: %d patches, %d tiles, %d horizon windows out of the zone\n", name, patches,
                tileCount, windows);
    return sums;
}

// The places on land along the rivers of the small world: the first and the middle vertex of every
// kRiverStride river segments whose patch, tiles of LOD 0 to 2 and horizon windows of levels 0 to
// 2 keep out of the zone.
constexpr std::size_t kRiverStride = 5;
constexpr int kRiverPlaces = 48;

Sums riverSums(const terrain::TerrainTiles& tiles, const terrain::PlanetLevel& planet) {
    const Heightfield& field = tiles.heightfield();
    terrain::HorizonParams params;
    params.levels = 3;
    params.cellsPerSide = 64;
    params.world = field.world();
    const terrain::HorizonSampler sampler(field, tiles.ecology(), params);
    Sums sums;
    int places = 0;
    const auto& segments = planet.rivers().segments;
    for (std::size_t k = 0; k < 2 * segments.size() && places < kRiverPlaces; k += kRiverStride) {
        const auto& vertices = segments[k / 2].vertices;
        const terrain::RiverVertex& v =
            k % 2 == 0 ? vertices.front() : vertices[vertices.size() / 2];
        const Place p{v.x, v.z};
        const Reach wide = horizonReach(params, 2);
        if (!ofSide(Side::Land, coast_zone::coastSignal(field, p.x, p.z)) ||
            !reachOutside(field, p, wide)) {
            continue;
        }
        addPatch(sums.patches, field, p);
        for (int lod = 0; lod < 3; ++lod) {
            addTiles(sums.tiles, tiles, p, lod);
        }
        for (int level = 0; level < 3; ++level) {
            addHorizon(sums.horizon, sampler, horizonSnap(params, level)(p), level);
        }
        ++places;
    }
    std::printf("small world: %d places along its rivers out of the zone\n", places);
    TEST_CHECK(places == kRiverPlaces);
    return sums;
}

double seconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

void testDefaultWorld() {
    const auto start = std::chrono::steady_clock::now();
    const terrain::TerrainTiles tiles(kSeed);
    const Sums sums = worldSums("default world", tiles, 0x33C0A5711ULL, 10);
    std::printf("default world in %.1f s\n", seconds(start));
    golden::report("the default world out of the coastal zone",
                   {{"default world patches", sums.patches.value(), 0x12BBBAA19D5D777FULL},
                    {"default world tiles", sums.tiles.value(), 0x5D62471F2B5D2302ULL},
                    {"default world horizon", sums.horizon.value(), 0x784F4CC234FCB46EULL}});
}

void testSmallWorldWithWater() {
    const auto start = std::chrono::steady_clock::now();
    const Heightfield relief(kSeed, terrain::HeightfieldParams{}, golden::kSmallPlanet);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threads)),
        golden::smallPlanetParams(threads));
    auto hydrology = std::make_shared<const terrain::Hydrology>(kSeed, relief, planet);
    const terrain::TerrainTiles tiles(kSeed, terrain::HeightfieldParams{}, {}, std::nullopt,
                                      golden::kSmallPlanet, hydrology);
    std::printf("small world planet level and water in %.1f s\n", seconds(start));
    const Sums sums = worldSums("small world", tiles, 0x33C0A5722ULL, 8);
    std::printf("small world drawn places in %.1f s\n", seconds(start));
    const Sums rivers = riverSums(tiles, *planet);
    std::printf("small world rivers in %.1f s\n", seconds(start));
    golden::report("the small world with its water out of the coastal zone",
                   {{"small world planet level", planet->checksum(), 0xBFA4DE19A54DAD75ULL},
                    {"small world patches", sums.patches.value(), 0x964F078C18B8402DULL},
                    {"small world tiles", sums.tiles.value(), 0xD8A62B6563C902FDULL},
                    {"small world horizon", sums.horizon.value(), 0x159D43BA4E7127E6ULL},
                    {"small world rivers patches", rivers.patches.value(), 0x88EE902D42C05D86ULL},
                    {"small world rivers tiles", rivers.tiles.value(), 0x97B050BC63942232ULL},
                    {"small world rivers horizon", rivers.horizon.value(), 0xD0FCF8AE5E0690CDULL}});
}

} // namespace

int main() {
    testDefaultWorld();
    testSmallWorldWithWater();
    return test::result();
}
