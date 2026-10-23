// Summary of the edits for the far horizon (edit_summary.hpp): every cell of 4 x 4 m agrees with
// its columns read block by block; the footprints of the horizon samples, 8 m to 4 km, are made
// of whole cells; the height change of a hole, a razed area and a building, not of a tunnel nor
// of removed air; the bytes come back exactly, the same for the same edits.
#include "terrain/storage/edit_summary.hpp"
#include "terrain/storage/volume_edit.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::BlockEdit;
using terrain::EditCellSummary;
using terrain::EditMode;
using terrain::EditShape;
using terrain::ModificationStore;
using terrain::VolumeEdit;

const terrain::WorldSize kWorld{};

VolumeEdit box(EditMode mode, BlockCoord anchor, std::uint16_t x, std::uint16_t y,
               std::uint16_t z) {
    return {EditShape::Box, mode, anchor, {x, y, z}, 0};
}

// The cell of 4 x 4 columns from (x0, z0), read block by block between the heights low and high.
EditCellSummary cellByBlocks(const ModificationStore& store, int x0, int z0, int low, int high) {
    EditCellSummary cell;
    std::int64_t bottoms = 0;
    std::int64_t tops = 0;
    for (int z = z0; z < z0 + 4; ++z) {
        for (int x = x0; x < x0 + 4; ++x) {
            std::optional<int> top;
            int bottom = 0;
            bool open = false;
            std::optional<int> added;
            for (int y = high; y >= low; --y) {
                const std::optional<BlockEdit> edit = store.blockAt({x, y, z});
                if (edit == BlockEdit::Removed && !top) {
                    top = bottom = y;
                    open = true;
                } else if (edit == BlockEdit::Removed && open && bottom == y + 1) {
                    bottom = y;
                } else {
                    open = false;
                }
                if (edit == BlockEdit::Added && !added) {
                    added = y;
                }
            }
            if (top) {
                cell.removedTop = cell.removedColumns == 0 ? *top : std::max(cell.removedTop, *top);
                ++cell.removedColumns;
                bottoms += bottom;
            }
            if (added) {
                ++cell.addedColumns;
                tops += *added;
            }
        }
    }
    if (cell.removedColumns > 0) {
        cell.removedBottom = static_cast<std::int32_t>(
            std::floor(static_cast<double>(bottoms) / cell.removedColumns));
    }
    if (cell.addedColumns > 0) {
        cell.addedTop =
            static_cast<std::int32_t>(std::floor(static_cast<double>(tops) / cell.addedColumns));
    }
    return cell;
}

ModificationStore sampleEdits() {
    ModificationStore store;
    terrain::applyVolumeEdit(store, box(EditMode::Remove, {-20, -30, -12}, 40, 70, 30), kWorld);
    terrain::applyVolumeEdit(
        store, {EditShape::Explosion, EditMode::Remove, {3, -31, 5}, {}, 150}, kWorld);
    terrain::applyVolumeEdit(store, box(EditMode::Add, {-4, -10, -4}, 6, 25, 6), kWorld);
    terrain::applyVolumeEdit(store, box(EditMode::Remove, {-30, -60, 10}, 50, 3, 3), kWorld);
    terrain::applyVolumeEdit(store, box(EditMode::Restore, {0, -20, 0}, 2, 2, 2), kWorld);
    return store;
}

void testCells() {
    const ModificationStore store = sampleEdits();
    bool same = true;
    int edited = 0;
    for (const auto& [coord, chunk] : store.byChunk()) {
        const terrain::ChunkEditSummary cells = terrain::summarizeChunk(chunk);
        for (int cz = 0; cz < 4; ++cz) {
            for (int cx = 0; cx < 4; ++cx) {
                const EditCellSummary cell = cells[static_cast<std::size_t>(cz * 4 + cx)];
                same = same && cell == cellByBlocks(store, coord.x * 16 + cx * 4,
                                                    coord.z * 16 + cz * 4, -100, 100);
                edited += cell.empty() ? 0 : 1;
            }
        }
    }
    TEST_CHECK(same && edited > 40);
}

// The footprint of a sample of each level, centered on a multiple of its pitch: whole cells.
void testFootprints() {
    for (int level = 0; level < 10; ++level) {
        const double pitch = 8.0 * (1 << level);
        for (const double center : {0.0, pitch, -3.0 * pitch, 1'000'000.0 * pitch}) {
            const terrain::SummaryCellRange cells = terrain::summaryCells(center, pitch / 2.0);
            TEST_CHECK(static_cast<double>(cells.first) * 4.0 == center - pitch / 2.0);
            TEST_CHECK(static_cast<double>(cells.last + 1) * 4.0 == center + pitch / 2.0);
        }
    }
}

void testHeightChange() {
    EditCellSummary hole{16, 0, 95, 104, 0};       // dug from 104 down to 95, every column
    EditCellSummary half{8, 0, 95, 104, 0};        // half of the columns
    EditCellSummary building{0, 4, 0, 0, 119};     // a tower up to 119 on a quarter of the cell
    EditCellSummary tunnel{16, 0, 40, 44, 0};      // under the ground
    TEST_CHECK(terrain::heightChange(hole, 100.25) == 94.5 - 100.25);
    TEST_CHECK(terrain::heightChange(half, 100.25) == (94.5 - 100.25) / 2.0);
    TEST_CHECK(terrain::heightChange(building, 100.0) == (119.5 - 100.0) / 4.0);
    TEST_CHECK(terrain::heightChange(tunnel, 100.0) == 0.0);
    TEST_CHECK(terrain::heightChange(hole, 300.0) == 0.0); // deep under a higher ground
    TEST_CHECK(terrain::heightChange(hole, 60.0) == 0.0);  // removed air above the ground
    const std::vector<EditCellSummary> footprint = {hole, {}, {}, {}};
    TEST_CHECK(terrain::meanHeightChange(footprint, 100.25) == (94.5 - 100.25) / 4.0);

    // A razed area, read from the masks.
    ModificationStore store;
    terrain::applyVolumeEdit(store, box(EditMode::Remove, {0, 20, 0}, 64, 480, 64), kWorld);
    const terrain::RegionEditSummary razed = terrain::summarizeRegion(store, {0, 0});
    TEST_CHECK(razed.cells.size() == 16 * 16);
    TEST_CHECK(terrain::heightChange(razed.cells.at(0), 350.0) == 19.5 - 350.0);
}

void testBytes() {
    const ModificationStore store = sampleEdits();
    for (const terrain::RegionCoord region : terrain::regionsOf(store)) {
        const terrain::RegionEditSummary summary = terrain::summarizeRegion(store, region);
        const std::vector<std::uint8_t> bytes = terrain::serializeEditSummary(summary);
        TEST_CHECK(terrain::deserializeEditSummary(bytes) == summary);
    }
    const terrain::RegionEditSummary summary = terrain::summarizeRegion(store, {-1, -1});
    const std::vector<std::uint8_t> bytes = terrain::serializeEditSummary(summary);
    std::printf("summary of region (-1, -1): %zu cells, %zu bytes, FNV-1a 0x%08X\n",
                summary.cells.size(), bytes.size(), terrain::fnv1a(bytes));
    TEST_CHECK(bytes.size() == 86 && terrain::fnv1a(bytes) == 0x7B638D0EU);
    std::vector<std::uint8_t> bad = bytes;
    bad.push_back(0);
    bool rejected = false;
    try {
        static_cast<void>(terrain::deserializeEditSummary(bad));
    } catch (const terrain::StorageError&) {
        rejected = true;
    }
    TEST_CHECK(rejected);
}

} // namespace

int main() {
    testCells();
    testFootprints();
    testHeightChange();
    testBytes();
    return test::result();
}
