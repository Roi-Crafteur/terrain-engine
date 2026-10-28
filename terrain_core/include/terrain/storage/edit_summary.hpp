#pragma once

#include "terrain/storage/modifications.hpp"
#include "terrain/storage/region_file.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace terrain {

// Summary of the edits for the far horizon, taken from the masks: per cell of 4 x 4 m (16
// columns of blocks), what a far view can show of them. The footprint of a sample of the
// horizon (horizon_sampler.hpp: CellFootprint) is the square of its pitch (8 m << level)
// centered on a multiple of the pitch: its edges fall on multiples of 4 m, so the cells it
// covers make it up exactly, on every level. The horizon does not read it: it shows what the
// coarsest tiles show, which only see the blocks within 1 m of their lattice of 8 m
// (horizon_edits.hpp); this summary of the masks themselves is a format to send them where they
// are not.
inline constexpr int kSummaryCellBlocks = 4;
inline constexpr int kSummaryCellsPerChunk = kSectionSide / kSummaryCellBlocks;
inline constexpr int kSummaryCellsPerRegion = kRegionChunks * kSummaryCellsPerChunk;

struct EditCellSummary {
    std::uint8_t removedColumns = 0; // columns (of 16) holding a removed block
    std::uint8_t addedColumns = 0;   // columns holding an added block
    // Over the columns holding a removed block: the lowest block of the highest run of removed
    // blocks of each column, on average (rounded down), and the highest removed block.
    std::int32_t removedBottom = 0;
    std::int32_t removedTop = 0;
    // Over the columns holding an added block: their highest added block, on average (rounded
    // down).
    std::int32_t addedTop = 0;

    [[nodiscard]] bool empty() const { return removedColumns == 0 && addedColumns == 0; }
    friend bool operator==(const EditCellSummary&, const EditCellSummary&) = default;
};

// The 4 x 4 cells of a chunk, x fastest, from its lowest corner.
using ChunkEditSummary = std::array<EditCellSummary, kSummaryCellsPerChunk * kSummaryCellsPerChunk>;
[[nodiscard]] ChunkEditSummary summarizeChunk(const ChunkModifications& chunk);

// Height change of the ground of a cell whose generated height is ground, meters: the columns
// whose highest removed run holds the ground fall to its bottom, the added blocks above the
// ground raise it, each weighted by its share of the 16 columns. Removed blocks above the ground
// (air) or under it (tunnels) change nothing.
[[nodiscard]] double heightChange(const EditCellSummary& cell, double ground);
// Mean of heightChange over the cells of a footprint, the cells without edits included.
[[nodiscard]] double meanHeightChange(std::span<const EditCellSummary> cells, double ground);

// The cells [first, last] (indices of cells of 4 m, floor(meters / 4)) of the span
// [center - halfSide, center + halfSide) of a footprint along one axis.
struct SummaryCellRange {
    std::int64_t first = 0;
    std::int64_t last = -1;
};
[[nodiscard]] SummaryCellRange summaryCells(double center, double halfSide);

// The cells of a region with edits, by index z * kSummaryCellsPerRegion + x in the region.
struct RegionEditSummary {
    RegionCoord region;
    std::map<std::uint16_t, EditCellSummary> cells;

    friend bool operator==(const RegionEditSummary&, const RegionEditSummary&) = default;
};
[[nodiscard]] RegionEditSummary summarizeRegion(const ModificationStore& store,
                                                RegionCoord region);

// Bytes of a region summary, to send it where the masks are not: i32 region x, i32 region z,
// varint cell count, then per cell by increasing index: varint index step (the index minus the
// previous one minus 1, the first from -1), u8 removed columns, u8 added columns, then with
// removed columns varint zigzag removed bottom and varint (removed top - removed bottom), with
// added columns varint zigzag added top. The same summary always gives the same bytes.
[[nodiscard]] std::vector<std::uint8_t> serializeEditSummary(const RegionEditSummary& summary);
// Throws a StorageError (Invalid) unless bytes are a whole summary of valid cells.
[[nodiscard]] RegionEditSummary deserializeEditSummary(std::span<const std::uint8_t> bytes);

} // namespace terrain
