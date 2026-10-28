// The edited ground of the columns of the LOD 3 lattice (horizon_edit_columns.hpp) is the top of
// the LOD 3 tiles on them, within the rounding of their floats: a crater of 10 m, a mountain razed
// by a box of 1 km x 1 km x 500 m (its floor shown 0.5 to 7.5 m over the true one, as the tiles
// show it), a heap of earth, across the edge of the world too. A tunnel under the ground and
// blocks removed in the air leave the columns as generated, bit for bit. The columns of the
// chunks an edit returns are each listed once, canonical and sorted, the same read from the tiles
// or from a store.
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/horizon_edit_columns.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::ChunkCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::TerrainTiles;
using terrain::VolumeEdit;

constexpr double kTileTolerance = 1e-3; // meters: the floats of Marching Cubes and of the levels
constexpr int kSpacing = terrain::kEditColumnSpacing;

VolumeEdit roundEdit(EditShape shape, EditMode mode, BlockCoord center, double radius) {
    return {shape, mode, center, {}, static_cast<std::uint16_t>(radius * 16.0)};
}

VolumeEdit box(EditMode mode, BlockCoord corner, std::uint16_t x, std::uint16_t y,
               std::uint16_t z) {
    return {EditShape::Box, mode, corner, {x, y, z}, 0};
}

int groundAt(const TerrainTiles& tiles, int x, int z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

// The highest vertex of the LOD 3 tile holding the column (x, z) on it.
std::optional<double> tileTop(const TerrainTiles& tiles, int x, int z) {
    const ChunkCoord chunk = terrain::worldToChunk(x, z);
    const terrain::TileData data =
        tiles.generate({terrain::tileContaining(chunk, terrain::kMaxLod), 0});
    std::optional<double> top;
    for (std::size_t n = 0; n < data.mesh.vertices.size(); ++n) {
        const streaming_test::WorldPoint p = streaming_test::worldVertex(data, n);
        if (p.x == x && p.z == z) {
            top = top ? std::max(*top, p.y) : p.y;
        }
    }
    return top;
}

std::optional<float> columnGround(const TerrainTiles& tiles, int x, int z) {
    const terrain::EditWindow window = tiles.editWindow(x - 1, x + 1, z - 1, z + 1);
    return terrain::editedColumnGround(window, tiles.heightfield().heightAt(x, z), x, z);
}

// Columns of a square around (x, z), 8 m apart: the largest gap to the tiles and how many the
// edits changed.
struct Agreement {
    double gap = 0.0;
    int edited = 0;
    int columns = 0;
};

Agreement compare(const TerrainTiles& tiles, int x0, int z0, int halfColumns) {
    Agreement result;
    for (int k = -halfColumns; k <= halfColumns; ++k) {
        for (int i = -halfColumns; i <= halfColumns; ++i) {
            const int x = x0 + i * kSpacing;
            const int z = z0 + k * kSpacing;
            const std::optional<float> edited = columnGround(tiles, x, z);
            const double ground = edited ? *edited : tiles.heightfield().heightAt(x, z);
            const std::optional<double> top = tileTop(tiles, x, z);
            result.gap = std::max(result.gap, top ? std::abs(*top - ground) : 1e9);
            result.edited += edited ? 1 : 0;
            ++result.columns;
        }
    }
    return result;
}

void testCrater() {
    TerrainTiles tiles(streaming_test::kSeed);
    const int x = streaming_test::kCenterChunkX * 16 + 8;
    const int z = streaming_test::kCenterChunkZ * 16 + 8;
    tiles.applyEdit(
        roundEdit(EditShape::Sphere, EditMode::Remove, {x, groundAt(tiles, x, z), z}, 5.0));
    const Agreement crater = compare(tiles, x, z, 2);
    std::printf("crater of 10 m: %d of %d columns edited, largest gap to the tiles %.2e m\n",
                crater.edited, crater.columns, crater.gap);
    TEST_CHECK(crater.edited > 0 && crater.gap <= kTileTolerance);
    TEST_CHECK(*columnGround(tiles, x, z) < tiles.heightfield().heightAt(x, z) - 2.0f);
}

void testHeap() {
    TerrainTiles tiles(streaming_test::kSeed);
    const int x = streaming_test::kCenterChunkX * 16 + 40;
    const int z = streaming_test::kCenterChunkZ * 16 - 24;
    const int ground = groundAt(tiles, x, z);
    tiles.applyEdit(roundEdit(EditShape::Sphere, EditMode::Add, {x, ground, z}, 24.0));
    const Agreement heap = compare(tiles, x, z, 4);
    std::printf("heap of earth of 24 m: %d of %d columns edited, largest gap %.2e m\n", heap.edited,
                heap.columns, heap.gap);
    TEST_CHECK(heap.edited > 9 && heap.gap <= kTileTolerance);
    TEST_CHECK(*columnGround(tiles, x, z) > static_cast<float>(ground + 16));
}

// The highest ground of a square, every 25 m.
int highestGround(const TerrainTiles& tiles, int x0, int z0, int side) {
    int top = groundAt(tiles, x0, z0);
    for (int dz = 0; dz <= side; dz += 25) {
        for (int dx = 0; dx <= side; dx += 25) {
            top = std::max(top, groundAt(tiles, x0 + dx, z0 + dz));
        }
    }
    return top;
}

void testRazedMountain() {
    TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord mountain = streaming_test::mountainChunk(tiles.heightfield());
    const int x0 = mountain.x * 16 + 5;
    const int z0 = mountain.z * 16 + 7;
    const int floorY = highestGround(tiles, x0, z0, 999) - 499;
    tiles.applyEdit(box(EditMode::Remove, {x0, floorY, z0}, 1000, 500, 1000));
    // The middle (the floor) and a corner (the walls) of the razed square.
    const Agreement middle = compare(tiles, (x0 + 500) & ~7, (z0 + 500) & ~7, 3);
    const Agreement corner = compare(tiles, x0 & ~7, z0 & ~7, 3);
    double low = 1e9;
    double high = -1e9;
    for (int k = 0; k < 8; ++k) {
        for (int i = 0; i < 8; ++i) {
            const int x = ((x0 + 300) & ~7) + i * kSpacing;
            const int z = ((z0 + 300) & ~7) + k * kSpacing;
            const std::optional<float> ground = columnGround(tiles, x, z);
            const double above = ground ? *ground - (floorY - 0.5) : 1e9;
            low = std::min(low, above);
            high = std::max(high, above);
        }
    }
    std::printf("mountain razed from %d m: floor %d of %d columns, gap %.2e m; corner %d of %d, "
                "gap %.2e m; floor drawn %.2f to %.2f m over the true one\n",
                floorY, middle.edited, middle.columns, middle.gap, corner.edited, corner.columns,
                corner.gap, low, high);
    TEST_CHECK(middle.edited == middle.columns && middle.gap <= kTileTolerance);
    TEST_CHECK(corner.edited > 0 && corner.edited < corner.columns);
    TEST_CHECK(corner.gap <= kTileTolerance);
    TEST_CHECK(low >= 0.0 && high <= 8.0 && high - low < 1e-3); // one height, the whole floor
}

// A tunnel 20 m under the ground and blocks removed 30 m above it change no column.
void testUnchanged() {
    TerrainTiles tiles(streaming_test::kSeed);
    const int x = streaming_test::kCenterChunkX * 16 - 64;
    const int z = streaming_test::kCenterChunkZ * 16 + 8;
    const int ground = groundAt(tiles, x, z);
    std::vector<ChunkCoord> chunks =
        tiles.applyEdit(box(EditMode::Remove, {x - 20, ground - 24, z - 2}, 40, 4, 5));
    const std::vector<ChunkCoord> air =
        tiles.applyEdit(box(EditMode::Remove, {x - 20, ground + 30, z - 2}, 40, 6, 5));
    chunks.insert(chunks.end(), air.begin(), air.end());
    const terrain::EditWindowSource windows = [&tiles](int minX, int maxX, int minZ, int maxZ) {
        return tiles.editWindow(minX, maxX, minZ, maxZ);
    };
    const std::vector<terrain::EditedColumn> columns =
        terrain::editedColumns(tiles.heightfield(), chunks, windows);
    const bool unchanged = std::all_of(columns.begin(), columns.end(),
                                       [](const terrain::EditedColumn& c) { return !c.edited; });
    TEST_CHECK(!columns.empty() && unchanged);
    TEST_CHECK(compare(tiles, x, z, 2).gap <= kTileTolerance);
}

// The columns of the chunks of an edit across the edge x = world.x / 2: canonical, sorted, once
// each, as the columns read one by one, the same from the tiles and from a copy of the store.
void testColumnsAcrossTheEdge() {
    TerrainTiles tiles(streaming_test::kSeed);
    const terrain::WorldSize& world = tiles.world();
    const int edge = static_cast<int>(world.x / 2.0);
    const int z = streaming_test::kCenterChunkZ * 16 + 8;
    const std::vector<ChunkCoord> chunks = tiles.applyEdit(roundEdit(
        EditShape::Explosion, EditMode::Remove, {edge, groundAt(tiles, edge, z), z}, 9.0));
    const terrain::ModificationStore store = tiles.modifications();
    const terrain::EditWindowSource fromTiles = [&tiles](int minX, int maxX, int minZ, int maxZ) {
        return tiles.editWindow(minX, maxX, minZ, maxZ);
    };
    const terrain::EditWindowSource fromStore = [&](int minX, int maxX, int minZ, int maxZ) {
        return terrain::EditWindow(store, minX, maxX, minZ, maxZ, world);
    };
    const std::vector<terrain::EditedColumn> a =
        terrain::editedColumns(tiles.heightfield(), chunks, fromTiles);
    const std::vector<terrain::EditedColumn> b =
        terrain::editedColumns(tiles.heightfield(), chunks, fromStore);
    bool sorted = true;
    bool same = a.size() == b.size();
    bool alike = true;
    int east = 0;
    int west = 0;
    const std::int64_t half = static_cast<std::int64_t>(world.x) / kSpacing / 2;
    for (std::size_t n = 0; n < a.size(); ++n) {
        sorted = sorted && (n == 0 || a[n - 1].column < a[n].column);
        same = same && a[n].column == b[n].column && a[n].edited == b[n].edited;
        TEST_CHECK(a[n].column.x >= -half && a[n].column.x < half);
        if (!a[n].edited) {
            continue;
        }
        const auto x = static_cast<int>(a[n].column.x * kSpacing);
        const auto cz = static_cast<int>(a[n].column.z * kSpacing);
        alike = alike && columnGround(tiles, x, cz) == a[n].edited &&
                std::abs(*tileTop(tiles, x, cz) - *a[n].edited) <= kTileTolerance;
        (a[n].column.x < 0 ? east : west) += 1;
    }
    std::printf("explosion across the edge of the world: %zu columns, %d edited west of the "
                "edge, %d east\n",
                a.size(), west, east);
    TEST_CHECK(sorted && same && alike && east > 0 && west > 0);
}

} // namespace

int main() {
    testCrater();
    testHeap();
    testRazedMountain();
    testUnchanged();
    testColumnsAcrossTheEdge();
    return test::result();
}
