// Seams between tiles on the walls of the mountains by the K2 (phase 12A): on the sides between
// LOD 0 and 1, 1 and 2, 2 and 3 of coarse tiles around the tallest faces, every vertex of the
// stitched fine tile lies on the outline of the coarse one (seam_test::outlineTolerance: at most
// 0.5 mm) and every vertex of the coarse tile on the outline of the fine one; between two tiles
// of LOD 0 side by side, each lies on the outline of the other. Many of these tiles hold faces.
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "seam_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using seam_test::Plane;
using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;

// The tallest faces by the K2 (rock_walls_test) and the foot of its highest wall.
constexpr std::array<std::array<double, 2>, 2> kPlaces = {{{415'765.0, -11'414'542.0},
                                                           {416'017.0, -11'414'806.0}}};
constexpr double kCoarseTolerance = 1e-5; // coarse vertices on the fine outline: float rounding

struct Tally {
    int sides = 0;
    int withRock = 0; // sides of a fine tile holding faces
    double fineGap = 0.0;
    double coarseGap = 0.0;
};

// The fine tile of LOD lod along side `side` (0 east, 1 west, 2 north, 3 south) of the coarse tile
// coarse, at slot 0 or 1: its key, the seam it carries and the plane of the side.
struct Side {
    TileKey fine;
    unsigned seam = 0;
    Plane plane;
};

Side sideOf(const TileKey& coarse, int side, int slot) {
    const int fine = terrain::tileChunks(coarse.lod - 1);
    const int size = terrain::tileChunks(coarse.lod);
    const ChunkCoord c = coarse.origin;
    const double chunk = terrain::kChunkSizeMeters;
    const int lod = coarse.lod - 1;
    switch (side) {
    case 0:
        return {{{c.x + size, c.z + slot * fine}, lod},
                terrain::kSeamNegX,
                {true, (c.x + size) * chunk}};
    case 1:
        return {{{c.x - fine, c.z + slot * fine}, lod}, terrain::kSeamPosX, {true, c.x * chunk}};
    case 2:
        return {{{c.x + slot * fine, c.z + size}, lod},
                terrain::kSeamNegZ,
                {false, (c.z + size) * chunk}};
    default:
        return {{{c.x + slot * fine, c.z - fine}, lod}, terrain::kSeamPosZ, {false, c.z * chunk}};
    }
}

void measure(const TerrainTiles& tiles, const TileKey& coarseKey, Tally& tally) {
    const TileData coarse = tiles.generate({coarseKey, 0});
    for (int side = 0; side < 4; ++side) {
        for (int slot = 0; slot < 2; ++slot) {
            const Side s = sideOf(coarseKey, side, slot);
            const TileData fine = tiles.generate({s.fine, s.seam});
            const double first = (s.plane.alongX ? s.fine.origin.z : s.fine.origin.x) *
                                 terrain::kChunkSizeMeters;
            const double last = first + terrain::tileSizeMeters(s.fine.lod);
            std::size_t points = 0;
            tally.fineGap = std::max(
                tally.fineGap, seam_test::largestGap(fine, coarse, s.plane, first, last, points));
            tally.coarseGap = std::max(
                tally.coarseGap, seam_test::largestGap(coarse, fine, s.plane, first, last, points));
            ++tally.sides;
            tally.withRock += fine.rock.empty() ? 0 : 1;
        }
    }
}

void testLodSeams() {
    const TerrainTiles tiles(terrain::kDefaultWorldSeed);
    for (int lod = 1; lod <= terrain::kMaxLod; ++lod) {
        Tally tally;
        for (const auto& [x, z] : kPlaces) {
            const ChunkCoord chunk = terrain::worldToChunk(x, z);
            const int size = terrain::tileChunks(lod);
            for (const int dz : {-1, 0}) {
                for (const int dx : {-1, 0}) {
                    const ChunkCoord at{chunk.x + dx * size, chunk.z + dz * size};
                    measure(tiles, terrain::tileContaining(at, lod), tally);
                }
            }
        }
        std::printf("LOD %d / %d by the K2: %d sides, %d of tiles with faces; largest gap %.2g m "
                    "fine to coarse, %.2g m coarse to fine\n",
                    lod - 1, lod, tally.sides, tally.withRock, tally.fineGap, tally.coarseGap);
        TEST_CHECK(tally.withRock * 4 >= tally.sides);
        TEST_CHECK(tally.fineGap <= seam_test::outlineTolerance(lod - 1));
        TEST_CHECK(tally.coarseGap <= kCoarseTolerance);
    }
}

// Two LOD 0 tiles side by side: the same columns on the shared side, the same outline.
void testSameLod() {
    const TerrainTiles tiles(terrain::kDefaultWorldSeed);
    double gap = 0.0;
    int withRock = 0;
    int sides = 0;
    for (const auto& [x, z] : kPlaces) {
        const ChunkCoord c = terrain::worldToChunk(x, z);
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                const TileData a = tiles.generate({{{c.x + dx, c.z + dz}, 0}, 0});
                const TileData b = tiles.generate({{{c.x + dx + 1, c.z + dz}, 0}, 0});
                const double first = (c.z + dz) * terrain::kChunkSizeMeters;
                const Plane plane{true, (c.x + dx + 1) * terrain::kChunkSizeMeters};
                std::size_t points = 0;
                gap = std::max({gap,
                                seam_test::largestGap(a, b, plane, first, first + 16.0, points),
                                seam_test::largestGap(b, a, plane, first, first + 16.0, points)});
                withRock += a.rock.empty() && b.rock.empty() ? 0 : 1;
                ++sides;
            }
        }
    }
    std::printf("LOD 0 side by side by the K2: %d sides, %d of tiles with faces; largest gap "
                "%.2g m\n",
                sides, withRock, gap);
    TEST_CHECK(withRock * 2 >= sides);
    TEST_CHECK(gap <= kCoarseTolerance);
}

} // namespace

int main() {
    testLodSeams();
    testSameLod();
    return test::result();
}
