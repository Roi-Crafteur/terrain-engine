// Seams between LODs at the edge of craters dug by the player: spheres, boxes and explosions
// drawn at random across the sides between LOD 0 and 1, 1 and 2, 2 and 3, on the four sides of
// a coarse tile, on a mountain and in the forests of the center of the tests, in the world with
// caves, and across the edge x = world.x / 2 of the wrapped world. On each side, every vertex of
// the coarse tile lies on the outline of the stitched fine tile, and every vertex of the fine
// tile on the outline of the coarse one (seam_test::outlineTolerance: at most 0.5 mm). Before
// lod_seams.cpp the fine side left the coarse one by up to 0.45 m (LOD 0 / 1), 1.3 m (1 / 2) and
// 2.8 m (2 / 3). The stitched tiles are the same bits generated again, and on 4 threads.
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "seam_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

using seam_test::Plane;
using terrain::BlockCoord;
using terrain::ChunkCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;
using terrain::VolumeEdit;

// Coarse vertices on the fine outline: float rounding only, as in lod_seams_test.cpp.
constexpr double kCoarseTolerance = 1e-5;

// Pseudo-random draws (SplitMix64), the same on every platform.
class Draws {
public:
    explicit Draws(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // In [low, high).
    double uniform(double low, double high) {
        return low + (high - low) * static_cast<double>(next() >> 11) * 0x1.0p-53;
    }

private:
    std::uint64_t state_;
};

// A fine tile with its coarser neighbor across one of its sides, the side on finePlane for the
// fine tile and on coarsePlane for the coarse one (two images across the edge of the world).
struct SeamCase {
    TileKey fine;
    TileKey coarse;
    unsigned seam = 0;
    Plane finePlane;
    Plane coarsePlane;
};

// The fine tile of LOD lod at slot 0 or 1 along the side `side` of the coarse tile at c (a
// multiple of its size): 0 east of it, 1 west, 2 north, 3 south.
SeamCase seamCase(ChunkCoord c, int lod, int side, int slot) {
    const int fine = terrain::tileChunks(lod);
    const int coarse = terrain::tileChunks(lod + 1);
    const double chunk = terrain::kChunkSizeMeters;
    const TileKey coarseKey{c, lod + 1};
    SeamCase result;
    switch (side) {
    case 0:
        result = {{{c.x + coarse, c.z + slot * fine}, lod}, coarseKey, terrain::kSeamNegX,
                  {true, (c.x + coarse) * chunk}, {}};
        break;
    case 1:
        result = {{{c.x - fine, c.z + slot * fine}, lod}, coarseKey, terrain::kSeamPosX,
                  {true, c.x * chunk}, {}};
        break;
    case 2:
        result = {{{c.x + slot * fine, c.z + coarse}, lod}, coarseKey, terrain::kSeamNegZ,
                  {false, (c.z + coarse) * chunk}, {}};
        break;
    default:
        result = {{{c.x + slot * fine, c.z - fine}, lod}, coarseKey, terrain::kSeamPosZ,
                  {false, c.z * chunk}, {}};
        break;
    }
    result.coarsePlane = result.finePlane;
    return result;
}

// Range of the shared side along u: the side of the fine tile.
std::array<double, 2> sharedRange(const SeamCase& c) {
    const double first = (c.finePlane.alongX ? c.fine.origin.z : c.fine.origin.x) *
                         terrain::kChunkSizeMeters;
    return {first, first + terrain::tileSizeMeters(c.fine.lod)};
}

int groundAt(const TerrainTiles& tiles, double x, double z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

// A crater of the shape drawn across the side, centered on it near the ground: radii and sizes
// of 1.5 to 4 coarse cells, the center from half a radius below the ground to a third above.
VolumeEdit crater(const TerrainTiles& tiles, const SeamCase& c, EditShape shape, Draws& draws) {
    const double cell = terrain::tileCellMeters(c.coarse.lod);
    const auto [first, last] = sharedRange(c);
    const double u = std::floor(draws.uniform(first + 1.0, last - 1.0));
    const double x = c.finePlane.alongX ? c.finePlane.at : u;
    const double z = c.finePlane.alongX ? u : c.finePlane.at;
    const double radius = cell * draws.uniform(1.5, 4.0);
    const int y = groundAt(tiles, x, z) + static_cast<int>(draws.uniform(-0.5, 0.34) * radius);
    const BlockCoord center{static_cast<int>(x), y, static_cast<int>(z)};
    if (shape != EditShape::Box) {
        return {shape, EditMode::Remove, center, {}, static_cast<std::uint16_t>(radius * 16.0)};
    }
    std::array<std::uint16_t, 3> size{};
    for (std::uint16_t& side : size) {
        side = static_cast<std::uint16_t>(cell * draws.uniform(1.5, 4.0));
    }
    const BlockCoord anchor{center.x - size[0] / 2, center.y - size[1] / 2,
                            center.z - size[2] / 2};
    return {EditShape::Box, EditMode::Remove, anchor, size, 0};
}

// Largest gaps of the seams of a boundary between two LODs, over its craters.
struct Tally {
    int coarseLod = 1;
    int craters = 0;
    int meshed = 0; // craters whose side holds a coarse outline
    double fineGap = 0.0;
    double coarseGap = 0.0;
};

void measure(const TerrainTiles& tiles, const SeamCase& c, Tally& tally) {
    const TileData coarse = tiles.generate({c.coarse, 0});
    const TileData fine = tiles.generate({c.fine, c.seam});
    const auto [uMin, uMax] = sharedRange(c);
    std::size_t finePoints = 0;
    std::size_t coarsePoints = 0;
    const double fineGap = seam_test::largestGapAcross(fine, c.finePlane, coarse, c.coarsePlane,
                                                       uMin, uMax, finePoints);
    const double coarseGap = seam_test::largestGapAcross(coarse, c.coarsePlane, fine,
                                                         c.finePlane, uMin, uMax, coarsePoints);
    ++tally.craters;
    if (coarsePoints >= 2) {
        ++tally.meshed;
    }
    tally.fineGap = std::max(tally.fineGap, fineGap);
    tally.coarseGap = std::max(tally.coarseGap, coarseGap);
}

// Digs a crater across the side, measures the seam, then restores the natural ground.
void craterAt(TerrainTiles& tiles, const SeamCase& c, EditShape shape, Draws& draws,
              Tally& tally) {
    tiles.applyEdit(crater(tiles, c, shape, draws));
    measure(tiles, c, tally);
    tiles.replaceModifications({});
}

void report(const char* where, const std::vector<Tally>& tallies) {
    for (const Tally& t : tallies) {
        std::printf("%s, LOD %d / %d: %d craters, %d meshed on the side; largest gap %.2g m "
                    "fine to coarse, %.2g m coarse to fine\n",
                    where, t.coarseLod - 1, t.coarseLod, t.craters, t.meshed, t.fineGap,
                    t.coarseGap);
        TEST_CHECK(t.meshed * 4 >= t.craters * 3);
        TEST_CHECK(t.fineGap <= seam_test::outlineTolerance(t.coarseLod - 1));
        TEST_CHECK(t.coarseGap <= kCoarseTolerance);
    }
}

constexpr std::array<EditShape, 3> kShapes = {EditShape::Sphere, EditShape::Box,
                                              EditShape::Explosion};

// Craters of every shape on the sides of the coarse tiles of each LOD at the chunk c (a multiple
// of 64 chunks): perSide craters of each shape on each side listed.
void testAround(TerrainTiles& tiles, ChunkCoord c, const std::vector<int>& sides, int perSide,
                std::uint64_t seed, const char* where) {
    Draws draws(seed);
    std::vector<Tally> tallies;
    for (int lod = 0; lod < terrain::kMaxLod; ++lod) {
        Tally tally;
        tally.coarseLod = lod + 1;
        for (const int side : sides) {
            for (const EditShape shape : kShapes) {
                for (int n = 0; n < perSide; ++n) {
                    craterAt(tiles, seamCase(c, lod, side, n % 2), shape, draws, tally);
                }
            }
        }
        tallies.push_back(tally);
    }
    report(where, tallies);
}

// Craters across the edge x = world.x / 2: the fine tile at the east end of the canonical range,
// its coarse neighbor at the west end, both sides of the same seam.
void testAcrossTheEdge(TerrainTiles& tiles) {
    const ChunkCoord count = terrain::worldChunks(terrain::WorldSize{});
    constexpr int kRow = -641774;
    const double edge = count.x * terrain::kChunkSizeMeters / 2.0;
    Draws draws(0xED6E);
    std::vector<Tally> tallies;
    for (int lod = 0; lod < terrain::kMaxLod; ++lod) {
        Tally tally;
        tally.coarseLod = lod + 1;
        const TileKey coarse = terrain::tileContaining({-count.x / 2, kRow}, lod + 1);
        const int fine = terrain::tileChunks(lod);
        for (const EditShape shape : kShapes) {
            for (int slot = 0; slot < 2; ++slot) {
                const SeamCase c{{{count.x / 2 - fine, coarse.origin.z + slot * fine}, lod},
                                 coarse,
                                 terrain::kSeamPosX,
                                 {true, edge},
                                 {true, -edge}};
                craterAt(tiles, c, shape, draws, tally);
            }
        }
        tallies.push_back(tally);
    }
    report("across the edge of the world", tallies);
}

// Craters of every shape across the sides of every LOD at once, then their stitched fine tiles
// (and the coarse ones) generated twice in turn, then all together by 4 threads: the same bits.
void testDeterminism(TerrainTiles& tiles, ChunkCoord c) {
    Draws draws(0xDE7E);
    std::vector<terrain::TileRequest> requests;
    for (int lod = 0; lod < terrain::kMaxLod; ++lod) {
        for (int side = 0; side < 4; ++side) {
            const SeamCase seam = seamCase(c, lod, side, side % 2);
            const EditShape shape = kShapes[static_cast<std::size_t>(side % 3)];
            tiles.applyEdit(crater(tiles, seam, shape, draws));
            requests.push_back({seam.fine, seam.seam});
            requests.push_back({seam.coarse, 0});
        }
    }
    std::vector<TileData> first;
    for (const terrain::TileRequest& request : requests) {
        first.push_back(tiles.generate(request));
    }
    bool same = true;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        same = same && tiles.generate(requests[i]) == first[i];
    }
    std::vector<TileData> parallel(requests.size());
    std::vector<std::thread> workers;
    for (std::size_t w = 0; w < 4; ++w) {
        workers.emplace_back([&, w] {
            for (std::size_t i = w; i < requests.size(); i += 4) {
                parallel[i] = tiles.generate(requests[i]);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    std::printf("determinism: %zu tiles over craters, the same bits again and on 4 threads: %s\n",
                requests.size(), same && parallel == first ? "yes" : "no");
    TEST_CHECK(same && parallel == first);
    tiles.replaceModifications({});
}

} // namespace

int main() {
    TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord mountain = streaming_test::mountainChunk(tiles.heightfield());
    testAround(tiles, mountain, {0, 1, 2, 3}, 2, 0x5EA3, "mountain");
    testAround(tiles, {streaming_test::kCenterChunkX, streaming_test::kCenterChunkZ}, {0, 3}, 2,
               0xF0E5, "forests");
    testAcrossTheEdge(tiles);
    testDeterminism(tiles, mountain);
    TerrainTiles caves(streaming_test::kSeed, {}, {}, terrain::CaveParams{});
    testAround(caves, mountain, {1, 2}, 2, 0xCA7E, "mountain with caves");
    return test::result();
}
