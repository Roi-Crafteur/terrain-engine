// The world out of the zone of the plateaus (agent 46, tablelands.hpp) keeps every bit with them
// switched on: on the default world (no planet level), the samples (one by one and in batches),
// the columns of the rock, the tiles of LOD 0 to 3 (with and without seams) and the horizon
// windows of levels 0 to 6 of places drawn near the zone and anywhere, whose reach (what each reads
// around it, with a margin) keeps out of it, are the same switched on and off. In the zone, most
// points change. (The water and the dunes of a planet level shared by both come with the planets
// test.)
#include "terrain/chunk_coord.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/tablelands.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "coast_zone_support.hpp"
#include "golden_support.hpp"
#include "tablelands_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::HeightSample;

// A square keeps out of the zone when a lattice kGuardStep meters apart over it, kMargin meters
// wider on each side, does: the shares of the provinces rise over tens of kilometers.
constexpr double kMargin = 4'000.0;
constexpr double kGuardStep = 2'000.0;
// Places near the zone: within kNear meters of a point of it.
constexpr double kNear = 30'000.0;
// In the zone, at least kLeastChanged of kZonePoints points change.
constexpr int kZonePoints = 2'000;
constexpr int kLeastChanged = 1'000;

struct Place {
    double x = 0.0;
    double z = 0.0;
};

// What a sample reads around it: the square from (x, z) - before to (x, z) + after.
struct Reach {
    double before = 0.0;
    double after = 0.0;
};

bool inZone(const Heightfield& field, double x, double z) {
    const terrain::Tablelands& t = *field.tablelands();
    const double cx = terrain::noise::wrapCanonical(x, field.world().x);
    const double cz = terrain::noise::wrapCanonical(z, field.world().z);
    return t.mayHold(cx, cz) && t.at(cx, cz).any();
}

bool reachOutside(const Heightfield& field, const Place& p, const Reach& r) {
    const double step = kGuardStep;
    const double x0 = p.x - r.before - kMargin;
    const double z0 = p.z - r.before - kMargin;
    const double side = r.before + r.after + 2.0 * kMargin;
    for (double z = z0; z <= z0 + side + 0.5 * step; z += step) {
        for (double x = x0; x <= x0 + side + 0.5 * step; x += step) {
            if (inZone(field, x, z)) {
                return false;
            }
        }
    }
    return true;
}

void addSample(golden::Checksum& sum, const HeightSample& s) {
    for (const float v : {s.continentalness, s.macroContinentalness, s.plateContinentalness,
                          s.plateCollision, s.mountainMask, s.relief, s.base, s.regional,
                          s.mountain, s.detail, s.height, s.waterLevel, s.rock}) {
        sum.add(v);
    }
    sum.add(static_cast<std::uint64_t>(s.water), 1);
    sum.add(static_cast<std::uint64_t>(s.tableland), 1);
}

void addRock(golden::Checksum& sum, const terrain::RockColumn& r) {
    sum.add(r.ground);
    sum.add(r.uncarved);
    sum.add(static_cast<std::uint64_t>(r.faceCount), 1);
    for (std::size_t k = 0; k < r.faceCount; ++k) {
        sum.add(r.faces[k].foot);
        sum.add(r.faces[k].top);
        sum.add(r.faces[k].distance);
        sum.add(r.faces[k].weight);
    }
}

// A patch of 16 x 16 points 2 m apart: in one batch with their rock, and its first row point by
// point.
constexpr Reach kPatchReach{8.0, 40.0};
void addPatch(golden::Checksum& sum, const Heightfield& field, const Place& p) {
    std::vector<double> xs;
    std::vector<double> zs;
    for (int j = 0; j < 16; ++j) {
        for (int i = 0; i < 16; ++i) {
            xs.push_back(p.x + 2.0 * i);
            zs.push_back(p.z + 2.0 * j);
        }
    }
    std::vector<HeightSample> out(xs.size());
    std::vector<terrain::RockColumn> rock(xs.size());
    field.samplesAt(xs, zs, out, rock);
    for (std::size_t i = 0; i < out.size(); ++i) {
        addSample(sum, out[i]);
        addRock(sum, rock[i]);
    }
    for (std::size_t i = 0; i < 16; ++i) {
        addSample(sum, field.sampleAt(xs[i], zs[0]));
    }
}

terrain::TileKey keyOf(const Place& p, int lod) {
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(p.x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(p.z / terrain::kChunkSizeMeters))};
    return terrain::tileContaining(chunk, lod);
}

// A tile reads its columns, one cell beyond, and 8 m around for its plants (coast_outside_test).
Reach tileReach(int lod) {
    const double cell = terrain::tileCellMeters(lod);
    return {cell + 8.0, cell + 8.0 + terrain::tileSizeMeters(lod)};
}

Place tileOrigin(const Place& p, int lod) {
    const terrain::TileKey key = keyOf(p, lod);
    return {key.origin.x * terrain::kChunkSizeMeters, key.origin.z * terrain::kChunkSizeMeters};
}

void addTiles(golden::Checksum& sum, const terrain::TerrainTiles& tiles, const Place& p, int lod) {
    const terrain::TileKey key = keyOf(p, lod);
    golden::addTile(sum, tiles.generate({key, 0}));
    golden::addTile(sum, tiles.generate({key, terrain::kSeamPosX | terrain::kSeamNegXNegZ}));
}

// The horizon cells of 8 x 8 samples of a level around a place; they read two pitches around.
constexpr int kCells = 8;
constexpr int kLevels = 7;
Reach horizonReach(const terrain::HorizonParams& params, int level) {
    const auto pitch = static_cast<double>(terrain::levelPitch(params, level));
    const double half = (kCells / 2 + 2) * pitch;
    return {half, half};
}

Place horizonCenter(const terrain::HorizonParams& params, const Place& p, int level) {
    const terrain::HorizonIndex c = terrain::canonicalCenter(params, level, p.x, p.z);
    const int pitch = terrain::levelPitch(params, level);
    return {terrain::samplePosition(c.x, pitch), terrain::samplePosition(c.z, pitch)};
}

void addHorizon(golden::Checksum& sum, const terrain::HorizonSampler& sampler, const Place& p,
                int level) {
    const terrain::HorizonIndex c = terrain::canonicalCenter(sampler.params(), level, p.x, p.z);
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

// Points of the zone of the world on: the lattice of 32 km, its points in a province.
std::vector<Place> zonePoints(const Heightfield& field) {
    std::vector<Place> out;
    const terrain::WorldSize& w = field.world();
    constexpr double kStep = 32'768.0;
    for (double z = -0.5 * w.z + 0.5 * kStep; z < 0.5 * w.z; z += kStep) {
        for (double x = -0.5 * w.x + 0.5 * kStep; x < 0.5 * w.x; x += kStep) {
            if (field.tablelandAt(x, z).tableland > 0.0f) {
                out.push_back({x, z});
            }
        }
    }
    return out;
}

struct Sums {
    golden::Checksum patches;
    golden::Checksum tiles;
    golden::Checksum horizon;
};

// What is read out of the zone around the places: a patch (level -1), the tiles of a LOD (level
// lod, tile true) or a horizon window of a level, at a place.
struct Item {
    Place place;
    int level = -1;
    bool tile = false;
};

terrain::HorizonParams horizonParams(const Heightfield& field) {
    terrain::HorizonParams params;
    params.levels = kLevels;
    params.cellsPerSide = 64;
    params.world = field.world();
    return params;
}

std::vector<Item> itemsOutside(const std::vector<Place>& places, const Heightfield& zone) {
    const terrain::HorizonParams params = horizonParams(zone);
    std::vector<Item> items;
    for (const Place& p : places) {
        if (reachOutside(zone, p, kPatchReach)) {
            items.push_back({p, -1, false});
        }
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            const Place origin = tileOrigin(p, lod);
            if (reachOutside(zone, origin, tileReach(lod))) {
                items.push_back({origin, lod, true});
            }
        }
        for (int level = 0; level < kLevels; ++level) {
            const Place center = horizonCenter(params, p, level);
            if (reachOutside(zone, center, horizonReach(params, level))) {
                items.push_back({center, level, false});
            }
        }
    }
    return items;
}

// The patches, tiles and horizon windows of the items, in the world of tiles.
Sums sumsOf(const terrain::TerrainTiles& tiles, const std::vector<Item>& items) {
    const Heightfield& field = tiles.heightfield();
    const terrain::HorizonSampler sampler(field, tiles.ecology(), horizonParams(field));
    Sums sums;
    for (const Item& item : items) {
        if (item.level < 0) {
            addPatch(sums.patches, field, item.place);
        } else if (item.tile) {
            addTiles(sums.tiles, tiles, item.place, item.level);
        } else {
            addHorizon(sums.horizon, sampler, item.place, item.level);
        }
    }
    return sums;
}

// Places near the zone (around its points, kNear meters at most) and anywhere on the world.
std::vector<Place> drawPlaces(const Heightfield& on, const std::vector<Place>& zone) {
    coast_zone::Draws draws(0x7AB1E1A2DULL);
    std::vector<Place> places;
    const terrain::WorldSize& w = on.world();
    for (int k = 0; k < 20 && !zone.empty(); ++k) {
        const Place& c = zone[static_cast<std::size_t>(draws.next() * static_cast<double>(zone.size()))];
        places.push_back({c.x + (draws.next() - 0.5) * 2.0 * kNear,
                          c.z + (draws.next() - 0.5) * 2.0 * kNear});
    }
    for (int k = 0; k < 16; ++k) {
        places.push_back({(draws.next() - 0.5) * w.x, (draws.next() - 0.5) * w.z});
    }
    return places;
}

} // namespace

int main() {
    const terrain::TerrainTiles off(terrain::kDefaultWorldSeed);
    const terrain::TerrainTiles on(terrain::kDefaultWorldSeed, tablelands_test::on());
    const Heightfield& field = on.heightfield();
    TEST_CHECK(off.heightfield().tablelands() == nullptr);
    const std::vector<Place> zone = zonePoints(field);
    const std::vector<Place> places = drawPlaces(field, zone);
    const std::vector<Item> items = itemsOutside(places, field);
    const Sums a = sumsOf(off, items);
    const Sums b = sumsOf(on, items);
    std::printf("%zu places, %zu of their patches, tiles and horizon windows out of the zone: "
                "patches %s, tiles %s, horizon %s\n",
                places.size(), items.size(),
                a.patches.value() == b.patches.value() ? "same" : "DIFFER",
                a.tiles.value() == b.tiles.value() ? "same" : "DIFFER",
                a.horizon.value() == b.horizon.value() ? "same" : "DIFFER");
    TEST_CHECK(items.size() >= 80);
    TEST_CHECK(a.patches.value() == b.patches.value());
    TEST_CHECK(a.tiles.value() == b.tiles.value());
    TEST_CHECK(a.horizon.value() == b.horizon.value());
    // In the zone: points around its points.
    coast_zone::Draws draws(0x2011E5ULL);
    int changed = 0;
    for (int k = 0; k < kZonePoints && !zone.empty(); ++k) {
        const Place& c = zone[static_cast<std::size_t>(draws.next() * static_cast<double>(zone.size()))];
        const double x = c.x + (draws.next() - 0.5) * 16'000.0;
        const double z = c.z + (draws.next() - 0.5) * 16'000.0;
        changed += off.heightfield().sampleAt(x, z).height != field.sampleAt(x, z).height ? 1 : 0;
    }
    std::printf("in the zone: %d of %d points change\n", changed, kZonePoints);
    TEST_CHECK(changed >= kLeastChanged);
    return test::result();
}
