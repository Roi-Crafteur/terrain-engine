// LOD: tile geometry, LOD by distance, the quadtree selection (coverage, balance, order,
// determinism), the seams of a selection and the stitching of a density grid. Seams on real
// relief: lod_seams_test.cpp.
#include "terrain/streaming/lod.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::DensityGrid;
using terrain::kGridSamples;
using terrain::LodParams;
using terrain::TileKey;

bool throws(const LodParams& params) {
    try {
        terrain::validate(params);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void testTileGeometry() {
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        TEST_CHECK(terrain::tileChunks(lod) == 1 << lod);
        TEST_CHECK(terrain::tileCellMeters(lod) == static_cast<double>(1 << lod));
        TEST_CHECK(terrain::tileSizeMeters(lod) == 16.0 * (1 << lod));
    }
    TEST_CHECK(terrain::tileContaining({0, 0}, 3) == TileKey{{0, 0}, 3});
    TEST_CHECK(terrain::tileContaining({-1, 5}, 2) == TileKey{{-4, 4}, 2});
    TEST_CHECK(terrain::tileContaining({-8, -9}, 3) == TileKey{{-8, -16}, 3});
    TEST_CHECK(terrain::tileContaining({7, -1}, 0) == TileKey{{7, -1}, 0});

    const TileKey tile{{0, 0}, 2}; // chunks 0 to 3 on each axis
    TEST_CHECK(terrain::tileContains(tile, {3, 3}) && !terrain::tileContains(tile, {4, 0}));
    TEST_CHECK(terrain::tileDistance(tile, {2, 2}) == 0);
    TEST_CHECK(terrain::tileDistance(tile, {5, 1}) == 2);
    TEST_CHECK(terrain::tileDistance(tile, {-1, -1}) == 1);
    TEST_CHECK(terrain::tileDistance(tile, {-3, 9}) == 6);
    TEST_CHECK(terrain::chunkDistance({1, 2}, {-3, 4}) == 4);

    const terrain::TileKeyHash hash;
    TEST_CHECK(hash({{0, 0}, 0}) != hash({{0, 0}, 1}));
    TEST_CHECK(hash({{1, 0}, 0}) != hash({{0, 1}, 0}));
}

// Canonical tiles: aligned on their size, the same key for every image, consistent with
// tileContaining on canonical chunks.
void testWrapTile() {
    const terrain::WorldSize world;
    const ChunkCoord count = terrain::worldChunks(world);
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const int size = terrain::tileChunks(lod);
        for (const ChunkCoord chunk :
             {ChunkCoord{0, 0}, ChunkCoord{-1, 5}, ChunkCoord{count.x / 2, 3},
              ChunkCoord{-count.x / 2 - 1, count.z / 2 + 9},
              ChunkCoord{3 * count.x + 17, -2 * count.z - 33}}) {
            const TileKey tile = terrain::tileContaining(chunk, lod);
            const TileKey wrapped = terrain::wrapTile(tile, world);
            TEST_CHECK(wrapped.lod == lod);
            TEST_CHECK(wrapped.origin.x % size == 0 && wrapped.origin.z % size == 0);
            TEST_CHECK(wrapped.origin.x >= -count.x / 2 && wrapped.origin.x + size <= count.x / 2);
            TEST_CHECK(wrapped.origin.z >= -count.z / 2 && wrapped.origin.z + size <= count.z / 2);
            TEST_CHECK(terrain::tileContaining(terrain::wrapChunk(chunk, world), lod) == wrapped);
            const TileKey image{{tile.origin.x + count.x, tile.origin.z - count.z}, lod};
            TEST_CHECK(terrain::wrapTile(image, world) == wrapped);
        }
    }
}

void testLodForDistance() {
    const LodParams params;
    const std::tuple<int, int> expected[] = {{0, 0},  {3, 0},  {4, 1},  {7, 1},
                                             {8, 2},  {15, 2}, {16, 3}, {500, 3}};
    for (const auto& [distance, lod] : expected) {
        TEST_CHECK(terrain::lodForDistance(distance, params) == lod);
    }
}

void testValidate() {
    TEST_CHECK(!throws(LodParams{}));
    TEST_CHECK(!throws(streaming_test::smallLod()));
    TEST_CHECK(!throws({{1, 3, 7}, 0}));
    TEST_CHECK(throws({{0, 8, 16}, 32}));  // no LOD 0 ring
    TEST_CHECK(throws({{4, 5, 16}, 32}));  // LOD 1 ring narrower than a LOD 1 tile
    TEST_CHECK(throws({{4, 8, 11}, 32}));  // LOD 2 ring narrower than a LOD 2 tile
    TEST_CHECK(throws({{4, 8, 16}, -1}));  // negative view distance
    bool selectThrows = false;
    try {
        static_cast<void>(terrain::selectTiles({0, 0}, {{4, 5, 16}, 32}));
    } catch (const std::invalid_argument&) {
        selectThrows = true;
    }
    TEST_CHECK(selectThrows);
}

struct ChunkLess {
    bool operator()(ChunkCoord a, ChunkCoord b) const {
        return std::tie(a.z, a.x) < std::tie(b.z, b.x);
    }
};

using ChunkTiles = std::map<ChunkCoord, std::vector<TileKey>, ChunkLess>;

ChunkTiles chunkTiles(const std::vector<TileKey>& tiles) {
    ChunkTiles owners;
    for (const TileKey& tile : tiles) {
        const int size = terrain::tileChunks(tile.lod);
        for (int dz = 0; dz < size; ++dz) {
            for (int dx = 0; dx < size; ++dx) {
                owners[{tile.origin.x + dx, tile.origin.z + dz}].push_back(tile);
            }
        }
    }
    return owners;
}

// Every chunk within the view lies in exactly one tile, and no two tiles overlap.
void checkCoverage(const std::vector<TileKey>& tiles, ChunkCoord player, const LodParams& params) {
    const ChunkTiles owners = chunkTiles(tiles);
    bool disjoint = true;
    for (const auto& [chunk, list] : owners) {
        disjoint = disjoint && list.size() == 1;
    }
    TEST_CHECK(disjoint);
    bool covered = true;
    const int reach = params.viewDistance;
    for (int z = player.z - reach; z <= player.z + reach; ++z) {
        for (int x = player.x - reach; x <= player.x + reach; ++x) {
            covered = covered && owners.contains({x, z});
        }
    }
    TEST_CHECK(covered);
}

// Tiles touching by a face or a corner differ by at most one LOD.
void checkBalance(const std::vector<TileKey>& tiles) {
    const ChunkTiles owners = chunkTiles(tiles);
    bool balanced = true;
    for (const TileKey& tile : tiles) {
        const int size = terrain::tileChunks(tile.lod);
        for (int dz = -1; dz <= size; ++dz) {
            for (int dx = -1; dx <= size; ++dx) {
                const auto it = owners.find({tile.origin.x + dx, tile.origin.z + dz});
                if (it != owners.end()) {
                    balanced = balanced && std::abs(it->second.front().lod - tile.lod) <= 1;
                }
            }
        }
    }
    TEST_CHECK(balanced);
}

// No tile is coarser than one of its chunks asks for, and its parent holds a chunk asking for
// its LOD or a finer one (it is not finer than needed).
void checkLodByDistance(const std::vector<TileKey>& tiles, ChunkCoord player,
                        const LodParams& params) {
    bool neverCoarser = true;
    bool neededFine = true;
    for (const TileKey& tile : tiles) {
        const int size = terrain::tileChunks(tile.lod);
        for (int dz = 0; dz < size; ++dz) {
            for (int dx = 0; dx < size; ++dx) {
                const int d = terrain::chunkDistance({tile.origin.x + dx, tile.origin.z + dz},
                                                     player);
                neverCoarser = neverCoarser && terrain::lodForDistance(d, params) >= tile.lod;
            }
        }
        if (tile.lod < terrain::kMaxLod) {
            const TileKey parent = terrain::tileContaining(tile.origin, tile.lod + 1);
            const int d = terrain::tileDistance(parent, player);
            neededFine = neededFine && terrain::lodForDistance(d, params) <= tile.lod;
        }
    }
    TEST_CHECK(neverCoarser);
    TEST_CHECK(neededFine);
}

void checkSelection(ChunkCoord player, const LodParams& params) {
    const std::vector<TileKey> tiles = terrain::selectTiles(player, params);
    TEST_CHECK(!tiles.empty());
    checkCoverage(tiles, player, params);
    checkBalance(tiles);
    checkLodByDistance(tiles, player, params);
    bool aligned = true;
    bool sorted = true;
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const int size = terrain::tileChunks(tiles[i].lod);
        aligned = aligned && terrain::tileContaining(tiles[i].origin, tiles[i].lod) == tiles[i] &&
                  tiles[i].origin.x % size == 0 && tiles[i].origin.z % size == 0;
        sorted = sorted && (i == 0 || terrain::loadsBefore(tiles[i - 1], tiles[i], player));
    }
    TEST_CHECK(aligned);
    TEST_CHECK(sorted);
    // The player stands in a LOD 0 tile, the first to load.
    TEST_CHECK(tiles.front() == (TileKey{player, 0}));
    TEST_CHECK(terrain::selectTiles(player, params) == tiles); // deterministic
}

void testSelection() {
    const ChunkCoord players[] = {{0, 0}, {-13, 7}, {1000, -2001}};
    for (const ChunkCoord player : players) {
        checkSelection(player, LodParams{});
    }
    checkSelection({3, -5}, streaming_test::smallLod());
    checkSelection({-1, -1}, {{1, 3, 7}, 9});
}

// With the default rings, the LOD of the tile of a chunk follows its distance: exactly 0 within
// 4 chunks, at least 1 from 5, 2 from 11 and exactly 3 from 24 (a tile may be one level finer
// than its farthest chunks ask for, never coarser).
void testLodRings() {
    const LodParams params;
    const ChunkCoord player{-37, 21};
    const ChunkTiles owners = chunkTiles(terrain::selectTiles(player, params));
    bool rings = true;
    for (const auto& [chunk, list] : owners) {
        const int d = terrain::chunkDistance(chunk, player);
        const int lod = list.front().lod;
        const int asked = terrain::lodForDistance(d, params);
        rings = rings && lod <= asked && lod >= asked - 1;
        rings = rings && (d >= 4 || lod == 0) && (d < 5 || lod >= 1) && (d < 11 || lod >= 2) &&
                (d < 24 || lod == 3);
    }
    TEST_CHECK(rings);
    // Beyond the view, nothing is selected unless its LOD 3 tile reaches into the view.
    const std::vector<TileKey> tiles = terrain::selectTiles(player, params);
    bool withinView = true;
    for (const TileKey& tile : tiles) {
        withinView = withinView && terrain::tileDistance(tile, player) <= params.viewDistance;
    }
    TEST_CHECK(withinView);
}

unsigned seamsOf(const std::vector<terrain::TileRequest>& requests, TileKey key) {
    for (const terrain::TileRequest& request : requests) {
        if (request.key == key) {
            return request.seams;
        }
    }
    return ~0U;
}

// A LOD 1 tile A (chunks 0 and 1 on each axis) with LOD 0 tiles along its +X and +Z sides and
// one only touching its +X +Z corner.
void testSeamsOfSelection() {
    const TileKey a{{0, 0}, 1};
    const TileKey b{{2, 0}, 0};
    const TileKey c{{2, 1}, 0};
    const TileKey d{{2, 2}, 0};
    const TileKey e{{0, 2}, 0};
    const TileKey f{{1, 2}, 0};
    const auto requests = terrain::tileRequests({a, b, c, d, e, f});
    TEST_CHECK(requests.size() == 6 && requests[0].key == a && requests[5].key == f);
    TEST_CHECK(seamsOf(requests, a) == 0); // its neighbors are finer
    TEST_CHECK(seamsOf(requests, b) == (terrain::kSeamNegX | terrain::kSeamNegXPosZ));
    TEST_CHECK(seamsOf(requests, c) == (terrain::kSeamNegX | terrain::kSeamNegXNegZ));
    TEST_CHECK(seamsOf(requests, d) == terrain::kSeamNegXNegZ); // corner only
    TEST_CHECK(seamsOf(requests, e) == (terrain::kSeamNegZ | terrain::kSeamPosXNegZ));
    TEST_CHECK(seamsOf(requests, f) == (terrain::kSeamNegZ | terrain::kSeamNegXNegZ));
}

// On a real selection, a face is stitched exactly when the tile across it is coarser, and meets
// the horizon exactly when no tile lies across it.
void testSeamsMatchNeighbors() {
    const ChunkCoord player{5, -3};
    const std::vector<TileKey> tiles = terrain::selectTiles(player, LodParams{});
    const ChunkTiles owners = chunkTiles(tiles);
    bool matches = true;
    std::size_t stitched = 0;
    std::size_t outer = 0;
    for (const terrain::TileRequest& request : terrain::tileRequests(tiles)) {
        const TileKey& tile = request.key;
        const int size = terrain::tileChunks(tile.lod);
        const struct {
            ChunkCoord outside;
            unsigned seam;
        } faces[] = {{{tile.origin.x - 1, tile.origin.z}, terrain::kSeamNegX},
                     {{tile.origin.x + size, tile.origin.z}, terrain::kSeamPosX},
                     {{tile.origin.x, tile.origin.z - 1}, terrain::kSeamNegZ},
                     {{tile.origin.x, tile.origin.z + size}, terrain::kSeamPosZ}};
        for (const auto& face : faces) {
            const auto it = owners.find(face.outside);
            const bool coarser = it != owners.end() && it->second.front().lod == tile.lod + 1;
            matches = matches && coarser == ((request.seams & face.seam) != 0) &&
                      (it == owners.end()) == ((request.horizon & face.seam) != 0);
            stitched += coarser ? 1 : 0;
            outer += it == owners.end() ? 1 : 0;
        }
    }
    TEST_CHECK(matches);
    TEST_CHECK(stitched > 0 && outer > 0);
}

float pattern(int x, int y, int z) {
    return static_cast<float>(std::sin(0.7 * x + 1.3 * y) * std::cos(0.9 * z) + 0.01 * y);
}

DensityGrid patternGrid() {
    DensityGrid grid(0.0f);
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            for (int x = 0; x < kGridSamples; ++x) {
                grid.set(x, y, z, pattern(x, y, z));
            }
        }
    }
    return grid;
}

void testStitchFace() {
    DensityGrid grid = patternGrid();
    terrain::stitchSeams(grid, terrain::kSeamNegX);
    bool stitched = true;
    bool untouched = true;
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            for (int x = 1; x < kGridSamples; ++x) {
                untouched = untouched && grid.at(x, y, z) == pattern(x, y, z);
            }
            const float value = grid.at(0, y, z);
            const bool oddY = y % 2 != 0;
            const bool oddZ = z % 2 != 0;
            float expected = pattern(0, y, z);
            if (oddY && oddZ) {
                expected = (pattern(0, y - 1, z - 1) + pattern(0, y + 1, z - 1) +
                            pattern(0, y - 1, z + 1) + pattern(0, y + 1, z + 1)) / 4.0f;
            } else if (oddY) {
                expected = (pattern(0, y - 1, z) + pattern(0, y + 1, z)) / 2.0f;
            } else if (oddZ) {
                expected = (pattern(0, y, z - 1) + pattern(0, y, z + 1)) / 2.0f;
            }
            // The center of a coarse cell keeps the magnitude of its bilinear value, its sign
            // that of the side of the coarse outline it lies on (lod_seams_grid_test).
            const bool center = oddY && oddZ;
            const float actual = center ? std::abs(value) : value;
            stitched = stitched && std::abs(actual - (center ? std::abs(expected) : expected)) <=
                                       1e-6f;
        }
    }
    TEST_CHECK(stitched);
    TEST_CHECK(untouched);
}

void testStitchCorner() {
    DensityGrid grid = patternGrid();
    terrain::stitchSeams(grid, terrain::kSeamPosXPosZ);
    constexpr int kLast = kGridSamples - 1;
    bool exact = true;
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            for (int x = 0; x < kGridSamples; ++x) {
                float expected = pattern(x, y, z);
                if (x == kLast && z == kLast && y % 2 != 0) {
                    expected = (pattern(x, y - 1, z) + pattern(x, y + 1, z)) / 2.0f;
                }
                exact = exact && std::abs(grid.at(x, y, z) - expected) <= 1e-6f;
            }
        }
    }
    TEST_CHECK(exact);
}

// A density linear in x, y and z is unchanged: the stitching interpolates exactly.
void testStitchKeepsLinearDensity() {
    DensityGrid grid(0.0f);
    const auto linear = [](int x, int y, int z) {
        return 0.25f * static_cast<float>(x) - 0.5f * static_cast<float>(y) +
               0.125f * static_cast<float>(z) - 3.0f;
    };
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            for (int x = 0; x < kGridSamples; ++x) {
                grid.set(x, y, z, linear(x, y, z));
            }
        }
    }
    terrain::stitchSeams(grid, 0xFFU);
    bool same = true;
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            for (int x = 0; x < kGridSamples; ++x) {
                same = same && grid.at(x, y, z) == linear(x, y, z);
            }
        }
    }
    TEST_CHECK(same);
}

} // namespace

int main() {
    testTileGeometry();
    testWrapTile();
    testLodForDistance();
    testValidate();
    testSelection();
    testLodRings();
    testSeamsOfSelection();
    testSeamsMatchNeighbors();
    testStitchFace();
    testStitchCorner();
    testStitchKeepsLinearDensity();
    return test::result();
}
