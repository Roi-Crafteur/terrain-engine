// The edits seen by the tiles of LOD 1 to 3 (agent 41, edit_coarse.hpp). A coarse lattice point
// read only the blocks touching it: a block of 1 m vanished between two points and grew to a bump
// of a whole cell on one (2.2 m at LOD 1, 8.7 m at LOD 3). Now:
// - a single block, broken or placed at every place of a cell, leaves a relay at LOD 1 (a hollow
//   or a bump near it, never a cell), weaker at LOD 2, nothing at LOD 3;
// - a wall one block thick keeps its height at LOD 1;
// - a large hole keeps its walls within half a cell at every LOD;
// - the stitched sides of tiles of two LODs still meet over the edits.
#include "block_targeting.hpp"
#include "seam_test_support.hpp"
#include "test_assert.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::BlockEdit;
using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;
using terrain::Vec3f;

// The highest point of the mesh of a tile over (x, z), world meters.
std::optional<double> topAt(const TileData& data, double x, double z) {
    const terrain::Mesh& mesh = data.mesh;
    const double px = x - data.originX;
    const double pz = z - data.originZ;
    std::optional<double> top;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const Vec3f& a = mesh.vertices[mesh.indices[t]];
        const Vec3f& b = mesh.vertices[mesh.indices[t + 1]];
        const Vec3f& c = mesh.vertices[mesh.indices[t + 2]];
        const double d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::abs(d) < 1e-12) {
            continue;
        }
        const double l1 = ((b.z - c.z) * (px - c.x) + (c.x - b.x) * (pz - c.z)) / d;
        const double l2 = ((c.z - a.z) * (px - c.x) + (a.x - c.x) * (pz - c.z)) / d;
        const double l3 = 1.0 - l1 - l2;
        if (l1 < -1e-9 || l2 < -1e-9 || l3 < -1e-9) {
            continue;
        }
        const double y = data.originY + l1 * a.y + l2 * b.y + l3 * c.y;
        top = top ? std::max(*top, y) : y;
    }
    return top;
}

// The largest change of the top of the tiles of a LOD over the square of half side reach
// around (x, z), and where.
struct Change {
    double peak = 0.0;     // meters, signed: the change of largest size
    double distance = 0.0; // from (x, z) to where it is, meters
};

Change change(const TileData& before, const TileData& after, double x, double z, double reach) {
    Change c;
    for (double dz = -reach; dz <= reach; dz += 0.25) {
        for (double dx = -reach; dx <= reach; dx += 0.25) {
            const std::optional<double> a = topAt(before, x + dx, z + dz);
            const std::optional<double> b = topAt(after, x + dx, z + dz);
            if (!a || !b || std::abs(*b - *a) <= std::abs(c.peak)) {
                continue;
            }
            c.peak = *b - *a;
            c.distance = std::sqrt(dx * dx + dz * dz);
        }
    }
    return c;
}

ChunkCoord chunkOf(double x, double z) {
    return {static_cast<int>(std::floor(x / 16.0)), static_cast<int>(std::floor(z / 16.0))};
}

TileData tileOver(const TerrainTiles& tiles, double x, double z, int lod) {
    return tiles.generate({terrain::tileContaining(chunkOf(x, z), lod), 0});
}

// A block broken or placed by the tool at (x, z) on the ground, at every place of a cell of LOD 3.
void testSingleBlocks() {
    TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const double x0 = 40.0;
    const double z0 = 40.0;
    std::array<TileData, 4> before;
    for (int lod = 1; lod <= 3; ++lod) {
        before[static_cast<std::size_t>(lod)] = tileOver(tiles, x0 + 4.0, z0 + 4.0, lod);
    }
    for (const bool placing : {false, true}) {
        std::array<double, 4> largest{};
        std::array<double, 4> smallest{1e9, 1e9, 1e9, 1e9};
        std::array<double, 4> farthest{};
        for (int ox = 0; ox < 8; ++ox) {
            for (int oz = 0; oz < 8; oz += 3) {
                const double x = x0 + ox + 0.3;
                const double z = z0 + oz + 0.3;
                const double ground = tiles.heightfield().heightAt(x, z);
                const terrain::unreal::BuildHit hit{{x, ground, z}, {0.0, 1.0, 0.0}, 3.0};
                const auto target = terrain::unreal::buildTarget(
                    hit, terrain::unreal::BuildTool::Block,
                    placing ? terrain::unreal::BuildAction::Place
                            : terrain::unreal::BuildAction::Break);
                const BlockCoord b = target.edit.anchor;
                tiles.setBlock(b, placing ? BlockEdit::Added : BlockEdit::Removed);
                for (int lod = 1; lod <= 3; ++lod) {
                    const auto l = static_cast<std::size_t>(lod);
                    const TileData after = tileOver(tiles, x0 + 4.0, z0 + 4.0, lod);
                    const Change c = change(before[l], after, b.x, b.z, 10.0);
                    largest[l] = std::max(largest[l], std::abs(c.peak));
                    smallest[l] = std::min(smallest[l], std::abs(c.peak));
                    farthest[l] = std::max(farthest[l], c.distance);
                }
                (void)tiles.clearBlock(b);
            }
        }
        for (int lod = 1; lod <= 3; ++lod) {
            const auto l = static_cast<std::size_t>(lod);
            std::printf("a block %s, LOD %d: the ground moves %.2f to %.2f m, at most %.2f m from "
                        "the block\n",
                        placing ? "placed" : "broken", lod, smallest[l], largest[l], farthest[l]);
        }
        TEST_CHECK(smallest[1] >= 0.2 && largest[1] <= 1.5 && farthest[1] <= 2.0);
        TEST_CHECK(largest[2] <= largest[1] + 1e-6);
        TEST_CHECK(largest[3] <= 0.05);
    }
}

// The top of the tiles of each LOD over a wall one block thick, 3 high and 8 long.
void testWall() {
    TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const int x = 45;
    const int z0 = 42;
    const int base = static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z0 + 4)));
    for (int z = z0; z < z0 + 8; ++z) {
        for (int y = base + 1; y <= base + 3; ++y) {
            tiles.setBlock({x, y, z}, BlockEdit::Added);
        }
    }
    std::array<double, 4> tops{};
    for (int lod = 0; lod <= 3; ++lod) {
        const TileData data = tileOver(tiles, x, z0 + 4, lod);
        double top = -1e9;
        for (double dx = -2.0; dx <= 2.0; dx += 0.25) {
            for (double dz = 1.0; dz <= 7.0; dz += 0.5) {
                if (const auto t = topAt(data, x + dx, z0 + dz)) {
                    top = std::max(top, *t);
                }
            }
        }
        tops[static_cast<std::size_t>(lod)] = top - base;
        std::printf("a wall 3 blocks high, LOD %d: its top %.2f m over its base\n", lod,
                    top - base);
    }
    TEST_CHECK(std::abs(tops[1] - tops[0]) <= 0.5);
}

// A hole of 16 x 8 x 16 blocks: its floor and its walls at every LOD.
void testLargeHole() {
    TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const int x0 = 37;
    const int z0 = 37;
    const int top = static_cast<int>(std::floor(tiles.heightfield().heightAt(x0 + 8, z0 + 8)));
    std::array<TileData, 4> before;
    for (int lod = 0; lod <= 3; ++lod) {
        before[static_cast<std::size_t>(lod)] = tileOver(tiles, x0 + 8, z0 + 8, lod);
    }
    for (int z = z0; z < z0 + 16; ++z) {
        for (int y = top - 7; y <= top + 2; ++y) {
            for (int x = x0; x < x0 + 16; ++x) {
                tiles.setBlock({x, y, z}, BlockEdit::Removed);
            }
        }
    }
    for (int lod = 0; lod <= 3; ++lod) {
        const auto l = static_cast<std::size_t>(lod);
        const TileData after = tileOver(tiles, x0 + 8, z0 + 8, lod);
        const double depth = *topAt(before[l], x0 + 8, z0 + 8) - *topAt(after, x0 + 8, z0 + 8);
        // The wall along x through the middle: where the ground has sunk half the depth.
        double wall = 0.0;
        for (double x = x0 + 8.0; x > x0 - 10.0; x -= 0.125) {
            const double sunk = *topAt(before[l], x, z0 + 8) - *topAt(after, x, z0 + 8);
            if (sunk < depth / 2.0) {
                wall = x;
                break;
            }
        }
        const double cell = terrain::tileCellMeters(lod);
        std::printf("a hole of 16 x 8 x 16 blocks, LOD %d: %.2f m deep, its wall at %.2f m (the "
                    "blocks end at %.1f)\n",
                    lod, depth, wall, x0 - 0.5);
        TEST_CHECK(depth > 6.0);
        TEST_CHECK(std::abs(wall - (x0 - 0.5)) <= cell / 2.0 + 0.5);
    }
}

// A hole across the side of a LOD 1 tile stitched to a LOD 2 tile, and of a LOD 0 tile stitched
// to a LOD 1 tile: no crack.
void testSeams() {
    TerrainTiles tiles(terrain::kDefaultWorldSeed);
    // The side x = 64 m: LOD 1 tile (64, 0)-(96, 32) west side; the LOD 2 tile (0, 0)-(64, 64).
    for (int z = 10; z < 22; ++z) {
        for (int x = 58; x < 70; ++x) {
            const int top = static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
            for (int y = top - 5; y <= top + 1; ++y) {
                tiles.setBlock({x, y, z}, BlockEdit::Removed);
            }
        }
    }
    struct Case {
        TileKey fine;
        TileKey coarse;
        unsigned seam;
    };
    const Case cases[] = {{{{4, 0}, 1}, {{0, 0}, 2}, terrain::kSeamNegX},
                          {{{4, 0}, 0}, {{2, 0}, 1}, terrain::kSeamNegX}};
    for (const Case& c : cases) {
        const TileData fine = tiles.generate({c.fine, c.seam});
        const TileData coarse = tiles.generate({c.coarse, 0});
        const seam_test::Plane plane{true, 64.0};
        const double side = terrain::tileSizeMeters(c.fine.lod);
        std::size_t points = 0;
        const double gap = seam_test::largestGap(fine, coarse, plane, 0.0, side, points);
        std::printf("seam of LOD %d on LOD %d over a hole: %zu points, largest gap %.6f m\n",
                    c.fine.lod, c.coarse.lod, points, gap);
        TEST_CHECK(points > 4 && gap <= seam_test::outlineTolerance(c.fine.lod));
    }
}

} // namespace

int main() {
    testSingleBlocks();
    testWall();
    testLargeHole();
    testSeams();
    return test::result();
}
