// The summaries of the edits for every level of the far horizon (horizon_edits.hpp): the
// footprint of a sample sums the edited columns it holds, as a brute force over the columns gives
// them, on levels 0 to 9; level 0 is the ground of the tiles bit for bit and a razed footprint
// its floor; the same edits staged in any order or grouping give the same bits; restored edits
// leave nothing; the areas to compute again stay narrow across the edge of the world; the
// samplers without edits keep their bits.
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/horizon_edits.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::CellFootprint;
using terrain::ChunkCoord;
using terrain::EditColumn;
using terrain::EditMode;
using terrain::EditShape;
using terrain::HorizonEditMap;
using terrain::TerrainTiles;
using terrain::VolumeEdit;

constexpr int kSpacing = terrain::kEditColumnSpacing;
const int kX = streaming_test::kCenterChunkX * 16;
const int kZ = streaming_test::kCenterChunkZ * 16;

int groundAt(const TerrainTiles& tiles, int x, int z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

VolumeEdit roundEdit(EditShape shape, EditMode mode, BlockCoord center, double radius) {
    return {shape, mode, center, {}, static_cast<std::uint16_t>(radius * 16.0)};
}

// A box removing the ground of a square from 20 m under its lowest point up into the air.
VolumeEdit razedSquare(const TerrainTiles& tiles, int x0, int z0, int side) {
    int low = groundAt(tiles, x0, z0);
    int high = low;
    for (int dz = 0; dz <= side; dz += 8) {
        for (int dx = 0; dx <= side; dx += 8) {
            low = std::min(low, groundAt(tiles, x0 + dx, z0 + dz));
            high = std::max(high, groundAt(tiles, x0 + dx, z0 + dz));
        }
    }
    const auto size = static_cast<std::uint16_t>(side);
    return {EditShape::Box,
            EditMode::Remove,
            {x0, low - 20, z0},
            {size, static_cast<std::uint16_t>(high - low + 40), size},
            0};
}

// A crater, a heap and a razed square of 200 m around (kX, kZ), and a crater 40 km away.
std::vector<VolumeEdit> sampleEdits(const TerrainTiles& tiles) {
    const int far = kX + 40'000;
    return {roundEdit(EditShape::Sphere, EditMode::Remove,
                      {kX + 8, groundAt(tiles, kX + 8, kZ + 8), kZ + 8}, 5.0),
            roundEdit(EditShape::Sphere, EditMode::Add,
                      {kX + 300, groundAt(tiles, kX + 300, kZ), kZ}, 30.0),
            razedSquare(tiles, kX - 400, kZ - 400, 200),
            roundEdit(EditShape::Explosion, EditMode::Remove, {far, groundAt(tiles, far, kZ), kZ},
                      20.0)};
}

// The edited columns of every edit, staged once.
std::vector<terrain::EditedColumn> allColumns(const TerrainTiles& tiles,
                                              const std::vector<ChunkCoord>& chunks) {
    const terrain::EditWindowSource windows = [&tiles](int minX, int maxX, int minZ, int maxZ) {
        return tiles.editWindow(minX, maxX, minZ, maxZ);
    };
    std::vector<terrain::EditedColumn> columns =
        terrain::editedColumns(tiles.heightfield(), chunks, windows);
    std::erase_if(columns, [](const terrain::EditedColumn& c) { return !c.edited; });
    return columns;
}

// The footprint of the sample of a level holding (x, z).
CellFootprint footprintOf(int level, double x, double z) {
    const double pitch = kSpacing << level;
    return {level, std::floor(x / pitch + 0.5) * pitch, std::floor(z / pitch + 0.5) * pitch,
            pitch / 2.0};
}

// The edited columns within a footprint, one by one.
terrain::FootprintEdits bruteForce(const std::vector<terrain::EditedColumn>& columns,
                                   const CellFootprint& f) {
    terrain::FootprintEdits brute;
    const auto side = static_cast<std::int64_t>(2.0 * f.halfSide) / kSpacing;
    brute.columns = side * side;
    for (const terrain::EditedColumn& c : columns) {
        const double x = static_cast<double>(c.column.x * kSpacing);
        const double z = static_cast<double>(c.column.z * kSpacing);
        if (x < f.centerX - f.halfSide || x >= f.centerX + f.halfSide ||
            z < f.centerZ - f.halfSide || z >= f.centerZ + f.halfSide) {
            continue;
        }
        brute.low = brute.edited == 0 ? *c.edited : std::min(brute.low, *c.edited);
        brute.high = brute.edited == 0 ? *c.edited : std::max(brute.high, *c.edited);
        ++brute.edited;
        brute.change += static_cast<double>(*c.edited) - c.ground;
    }
    return brute;
}

// The footprints of levels 0 to 9 around the edits, against a brute force over the columns.
void testFootprints() {
    TerrainTiles tiles(streaming_test::kSeed);
    std::vector<ChunkCoord> chunks;
    for (const VolumeEdit& edit : sampleEdits(tiles)) {
        const std::vector<ChunkCoord> touched = tiles.applyEdit(edit);
        chunks.insert(chunks.end(), touched.begin(), touched.end());
    }
    HorizonEditMap map(tiles.heightfield());
    map.stage(tiles, chunks);
    TEST_CHECK(map.hasStaged() && map.empty());
    const std::vector<terrain::HorizonArea> areas = map.applyStaged();
    const std::vector<terrain::EditedColumn> columns = allColumns(tiles, chunks);
    TEST_CHECK(!map.hasStaged() && map.columnCount() == columns.size());
    TEST_CHECK(areas.size() >= 2); // the edits 40 km apart
    bool same = true;
    int footprints = 0;
    int edited = 0;
    for (int level = 0; level < 10; ++level) {
        const int pitch = kSpacing << level;
        for (const VolumeEdit& edit : sampleEdits(tiles)) {
            for (int k = -2; k <= 2; ++k) {
                for (int i = -2; i <= 2; ++i) {
                    const CellFootprint f =
                        footprintOf(level, edit.anchor.x + i * pitch, edit.anchor.z + k * pitch);
                    const terrain::FootprintEdits sum = map.footprint(f);
                    const terrain::FootprintEdits brute = bruteForce(columns, f);
                    same = same && sum.columns == brute.columns && sum.edited == brute.edited &&
                           std::abs(sum.change - brute.change) <=
                               1e-9 * (1.0 + std::abs(brute.change)) &&
                           (brute.edited == 0 || (sum.low == brute.low && sum.high == brute.high));
                    ++footprints;
                    edited += brute.edited > 0 ? 1 : 0;
                }
            }
        }
    }
    std::printf("%zu edited columns, %zu areas; %d footprints of levels 0 to 9, %d edited; map "
                "of %zu bytes\n",
                columns.size(), areas.size(), footprints, edited, map.heapBytes());
    TEST_CHECK(same);
    TEST_CHECK(edited > 100);
}

// Level 0 is the ground of the tiles; a footprint the razed square covers is its floor.
void testSummaries() {
    TerrainTiles tiles(streaming_test::kSeed);
    const VolumeEdit razed = sampleEdits(tiles)[2];
    HorizonEditMap map(tiles.heightfield());
    map.stage(tiles, tiles.applyEdit(razed));
    map.applyStaged();
    const double x = razed.anchor.x + 96.0;
    const double z = razed.anchor.z + 96.0;
    const float ground = tiles.heightfield().heightAt(x, z);
    const std::optional<terrain::EditSummary> point = map.summary(footprintOf(0, x, z));
    const std::optional<float> edited =
        map.columnGround({static_cast<std::int64_t>(std::floor(x / kSpacing)),
                          static_cast<std::int64_t>(std::floor(z / kSpacing))});
    TEST_CHECK(point && edited && point->range && point->range->low == *edited);
    TEST_CHECK(point && terrain::editedHeight(*point, ground) == *edited);
    // Level 3 (64 m): the footprint around the middle of the square is razed whole.
    const std::optional<terrain::EditSummary> wide = map.summary(footprintOf(3, x, z));
    TEST_CHECK(wide && wide->range && wide->range->low == wide->range->high);
    TEST_CHECK(wide && terrain::editedHeight(*wide, ground + 300.0f) == wide->range->low);
    // Level 7 (1 km): partly razed, no range.
    const std::optional<terrain::EditSummary> part = map.summary(footprintOf(7, x, z));
    TEST_CHECK(part && !part->range && part->heightChange < 0.0f);
    TEST_CHECK(!map.summary(footprintOf(5, x + 20'000.0, z)));
}

// The same edits in two orders, staged edit by edit or all at once: the same bits.
void testOrders() {
    TerrainTiles forward(streaming_test::kSeed);
    TerrainTiles backward(streaming_test::kSeed);
    std::vector<VolumeEdit> edits = sampleEdits(forward);
    edits.erase(edits.begin() + 1); // the heap: its order against the removals matters
    HorizonEditMap stepwise(forward.heightfield());
    HorizonEditMap atOnce(backward.heightfield());
    std::vector<ChunkCoord> chunks;
    for (const VolumeEdit& edit : edits) {
        stepwise.stage(forward, forward.applyEdit(edit));
        stepwise.applyStaged();
    }
    for (auto edit = edits.rbegin(); edit != edits.rend(); ++edit) {
        const std::vector<ChunkCoord> touched = backward.applyEdit(*edit);
        chunks.insert(chunks.end(), touched.begin(), touched.end());
    }
    atOnce.stage(backward, chunks);
    atOnce.applyStaged();
    bool same = stepwise.columnCount() == atOnce.columnCount() && stepwise.columnCount() > 0;
    for (int level = 0; level < 10; ++level) {
        for (int step = -8; step <= 8; ++step) {
            const CellFootprint f = footprintOf(level, kX + step * (16 << level), kZ - 200.0);
            const terrain::FootprintEdits a = stepwise.footprint(f);
            const terrain::FootprintEdits b = atOnce.footprint(f);
            same =
                same && a.edited == b.edited &&
                std::bit_cast<std::uint64_t>(a.change) == std::bit_cast<std::uint64_t>(b.change) &&
                a.low == b.low && a.high == b.high;
        }
    }
    TEST_CHECK(same);
}

// Restored blocks: the map is empty again, and says where.
void testRestore() {
    TerrainTiles tiles(streaming_test::kSeed);
    HorizonEditMap map(tiles.heightfield());
    const VolumeEdit razed = sampleEdits(tiles)[2];
    map.stage(tiles, tiles.applyEdit(razed));
    map.applyStaged();
    TEST_CHECK(!map.empty());
    VolumeEdit restore = razed;
    restore.mode = EditMode::Restore;
    map.stage(tiles, tiles.applyEdit(restore));
    const std::vector<terrain::HorizonArea> areas = map.applyStaged();
    TEST_CHECK(map.empty() && areas.size() == 1);
    TEST_CHECK(areas.size() == 1 && areas[0].maxX - areas[0].minX <= 208.0);
    TEST_CHECK(!map.summary(footprintOf(3, razed.anchor.x + 100.0, razed.anchor.z + 100.0)));
    TEST_CHECK(map.applyStaged().empty()); // nothing staged: nothing changes
}

// An explosion across the edge x = world.x / 2: one narrow area, whichever side it is read from.
void testWorldEdge() {
    TerrainTiles tiles(streaming_test::kSeed);
    HorizonEditMap map(tiles.heightfield());
    const terrain::WorldSize& world = tiles.world();
    const int edge = static_cast<int>(world.x / 2.0);
    map.stage(tiles, tiles.applyEdit(roundEdit(EditShape::Explosion, EditMode::Remove,
                                               {edge, groundAt(tiles, edge, kZ), kZ}, 12.0)));
    const std::vector<terrain::HorizonArea> areas = map.applyStaged();
    TEST_CHECK(areas.size() == 1 && areas[0].maxX - areas[0].minX <= 32.0);
    // Level 2 (32 m): the footprint on the edge holds 4 x 4 columns, 2 x 4 on each side.
    const CellFootprint edgeFootprint = footprintOf(2, -world.x / 2.0, kZ);
    const terrain::FootprintEdits both = map.footprint(edgeFootprint);
    std::int64_t east = 0;
    std::int64_t west = 0;
    for (int dz = -2; dz < 2; ++dz) {
        const double z = edgeFootprint.centerZ + dz * kSpacing;
        for (int d = 0; d < 2; ++d) {
            east += map.footprint(footprintOf(0, -world.x / 2.0 + d * kSpacing, z)).edited;
            west += map.footprint(footprintOf(0, world.x / 2.0 - (d + 1) * kSpacing, z)).edited;
        }
    }
    TEST_CHECK(east > 0 && west > 0 && both.edited == east + west);
}

void testInvalid() {
    const terrain::Heightfield field(streaming_test::kSeed);
    const HorizonEditMap map(field);
    terrain::HorizonParams params;
    map.validate(params);
    int refused = 0;
    params.basePitch = 4;
    try {
        map.validate(params);
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    params.basePitch = 8;
    params.world.x *= 2.0;
    try {
        map.validate(params);
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    try {
        static_cast<void>(map.footprint({0, 0.0, 0.0, 2.0}));
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    TEST_CHECK(refused == 3);
}

// A sampler reading an empty map gives the bits of a sampler without edits.
void testEmptyMap() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const HorizonEditMap map(tiles.heightfield());
    terrain::HorizonParams params;
    params.cellsPerSide = 64;
    const terrain::HorizonSampler plain(tiles.heightfield(), tiles.ecology(), params);
    const terrain::HorizonSampler read(tiles.heightfield(), tiles.ecology(), params, map.source());
    bool same = true;
    for (const int level : {0, 3, 8}) {
        const terrain::HorizonIndex c = terrain::canonicalCenter(params, level, kX, kZ);
        const terrain::HorizonRect rect{c.x - 4, c.z - 4, c.x + 4, c.z + 4};
        same = same && plain.sample(level, rect) == read.sample(level, rect);
    }
    TEST_CHECK(same);
}

} // namespace

int main() {
    testFootprints();
    testSummaries();
    testOrders();
    testRestore();
    testWorldEdge();
    testInvalid();
    testEmptyMap();
    return test::result();
}
