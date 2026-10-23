#include "terrain/storage/edit_summary.hpp"

#include "byte_stream.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace terrain {

namespace {

constexpr int kColumnsPerCell = kSummaryCellBlocks * kSummaryCellBlocks;
constexpr int kCellIndices = kSummaryCellsPerRegion * kSummaryCellsPerRegion;

// The edits of a column of blocks, read from the top down.
struct ColumnEdits {
    std::optional<int> removedTop; // the highest removed block
    int removedBottom = 0;         // the lowest block of its run so far
    bool open = false;             // its run may still grow downward
    std::optional<int> addedTop;   // the highest added block

    // The blocks [low, high], all in one state, below every block read before.
    void read(int high, int low, std::optional<BlockEdit> edit) {
        if (edit == BlockEdit::Removed) {
            if (!removedTop) {
                removedTop = high;
                removedBottom = low;
                open = true;
            } else if (open && removedBottom == high + 1) {
                removedBottom = low;
            } else {
                open = false;
            }
            return;
        }
        open = false;
        if (edit == BlockEdit::Added && !addedTop) {
            addedTop = high;
        }
    }
};

using ChunkColumns = std::array<ColumnEdits, kSectionSide * kSectionSide>;

void readSection(ChunkColumns& columns, std::int32_t sy, const BlockSection& section) {
    const int base = sy * kSectionSide;
    if (const std::optional<BlockEdit> uniform = section.uniform()) {
        for (ColumnEdits& column : columns) {
            column.read(base + kSectionSide - 1, base, uniform);
        }
        return;
    }
    for (int z = 0; z < kSectionSide; ++z) {
        for (int x = 0; x < kSectionSide; ++x) {
            ColumnEdits& column = columns[static_cast<std::size_t>(z * kSectionSide + x)];
            for (int y = kSectionSide - 1; y >= 0; --y) {
                if (const std::optional<BlockEdit> edit = section.at(sectionIndex(x, y, z))) {
                    column.read(base + y, base + y, edit);
                } else {
                    column.open = false;
                }
            }
        }
    }
}

// sum / count rounded down.
std::int32_t floorMean(std::int64_t sum, int count) {
    std::int64_t mean = sum / count;
    if (sum % count != 0 && sum < 0) {
        --mean;
    }
    return static_cast<std::int32_t>(mean);
}

EditCellSummary summarizeCell(const ChunkColumns& columns, int cellX, int cellZ) {
    EditCellSummary cell;
    std::int64_t bottoms = 0;
    std::int64_t tops = 0;
    int removed = 0;
    int added = 0;
    for (int dz = 0; dz < kSummaryCellBlocks; ++dz) {
        for (int dx = 0; dx < kSummaryCellBlocks; ++dx) {
            const ColumnEdits& column = columns[static_cast<std::size_t>(
                (cellZ * kSummaryCellBlocks + dz) * kSectionSide + cellX * kSummaryCellBlocks +
                dx)];
            if (column.removedTop) {
                cell.removedTop = removed == 0 ? *column.removedTop
                                               : std::max(cell.removedTop, *column.removedTop);
                bottoms += column.removedBottom;
                ++removed;
            }
            if (column.addedTop) {
                tops += *column.addedTop;
                ++added;
            }
        }
    }
    cell.removedColumns = static_cast<std::uint8_t>(removed);
    cell.addedColumns = static_cast<std::uint8_t>(added);
    cell.removedBottom = removed == 0 ? 0 : floorMean(bottoms, removed);
    cell.addedTop = added == 0 ? 0 : floorMean(tops, added);
    return cell;
}

std::uint32_t zigzag(std::int32_t value) {
    return (static_cast<std::uint32_t>(value) << 1) ^ static_cast<std::uint32_t>(value >> 31);
}

std::int32_t unzigzag(std::uint32_t value) {
    return static_cast<std::int32_t>((value >> 1) ^ (~(value & 1U) + 1U));
}

} // namespace

ChunkEditSummary summarizeChunk(const ChunkModifications& chunk) {
    ChunkColumns columns{};
    for (auto it = chunk.sections.rbegin(); it != chunk.sections.rend(); ++it) {
        readSection(columns, it->first, it->second);
    }
    ChunkEditSummary summary{};
    for (int z = 0; z < kSummaryCellsPerChunk; ++z) {
        for (int x = 0; x < kSummaryCellsPerChunk; ++x) {
            summary[static_cast<std::size_t>(z * kSummaryCellsPerChunk + x)] =
                summarizeCell(columns, x, z);
        }
    }
    return summary;
}

double heightChange(const EditCellSummary& cell, double ground) {
    double change = 0.0;
    if (cell.removedColumns > 0) {
        const double bottom = cell.removedBottom - 0.5;
        const double top = cell.removedTop + 0.5;
        if (ground >= bottom && ground <= top) {
            change += (bottom - ground) * cell.removedColumns / kColumnsPerCell;
        }
    }
    if (cell.addedColumns > 0) {
        const double top = cell.addedTop + 0.5;
        if (top > ground) {
            change += (top - ground) * cell.addedColumns / kColumnsPerCell;
        }
    }
    return change;
}

double meanHeightChange(std::span<const EditCellSummary> cells, double ground) {
    if (cells.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const EditCellSummary& cell : cells) {
        sum += heightChange(cell, ground);
    }
    return sum / static_cast<double>(cells.size());
}

SummaryCellRange summaryCells(double center, double halfSide) {
    return {static_cast<std::int64_t>(std::floor((center - halfSide) / kSummaryCellBlocks)),
            static_cast<std::int64_t>(std::ceil((center + halfSide) / kSummaryCellBlocks)) - 1};
}

RegionEditSummary summarizeRegion(const ModificationStore& store, RegionCoord region) {
    RegionEditSummary summary{region, {}};
    for (int dz = 0; dz < kRegionChunks; ++dz) {
        for (int dx = 0; dx < kRegionChunks; ++dx) {
            const ChunkModifications* chunk =
                store.chunk({region.x * kRegionChunks + dx, region.z * kRegionChunks + dz});
            if (chunk == nullptr || chunk->sections.empty()) {
                continue;
            }
            const ChunkEditSummary cells = summarizeChunk(*chunk);
            for (int cz = 0; cz < kSummaryCellsPerChunk; ++cz) {
                for (int cx = 0; cx < kSummaryCellsPerChunk; ++cx) {
                    const EditCellSummary& cell =
                        cells[static_cast<std::size_t>(cz * kSummaryCellsPerChunk + cx)];
                    if (!cell.empty()) {
                        const int x = dx * kSummaryCellsPerChunk + cx;
                        const int z = dz * kSummaryCellsPerChunk + cz;
                        summary.cells.emplace(
                            static_cast<std::uint16_t>(z * kSummaryCellsPerRegion + x), cell);
                    }
                }
            }
        }
    }
    return summary;
}

std::vector<std::uint8_t> serializeEditSummary(const RegionEditSummary& summary) {
    storage::ByteWriter out(12 + summary.cells.size() * 8);
    out.i32(summary.region.x);
    out.i32(summary.region.z);
    out.varint(static_cast<std::uint32_t>(summary.cells.size()));
    int previous = -1;
    for (const auto& [index, cell] : summary.cells) {
        out.varint(static_cast<std::uint32_t>(index - previous - 1));
        previous = index;
        out.u8(cell.removedColumns);
        out.u8(cell.addedColumns);
        if (cell.removedColumns > 0) {
            out.varint(zigzag(cell.removedBottom));
            out.varint(static_cast<std::uint32_t>(cell.removedTop - cell.removedBottom));
        }
        if (cell.addedColumns > 0) {
            out.varint(zigzag(cell.addedTop));
        }
    }
    return out.take();
}

RegionEditSummary deserializeEditSummary(std::span<const std::uint8_t> bytes) {
    storage::ByteReader in(bytes, "invalid edit summary: ");
    RegionEditSummary summary;
    summary.region.x = in.i32();
    summary.region.z = in.i32();
    const std::uint32_t count = in.varint();
    if (count > in.remaining() / 3) {
        in.fail("truncated");
    }
    std::int64_t previous = -1;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::int64_t index = previous + 1 + in.varint();
        EditCellSummary cell;
        cell.removedColumns = in.u8();
        cell.addedColumns = in.u8();
        if (index >= kCellIndices || cell.empty() || cell.removedColumns > kColumnsPerCell ||
            cell.addedColumns > kColumnsPerCell) {
            in.fail("invalid cell");
        }
        if (cell.removedColumns > 0) {
            cell.removedBottom = unzigzag(in.varint());
            const std::int64_t top = std::int64_t{cell.removedBottom} + in.varint();
            if (top > std::numeric_limits<std::int32_t>::max()) {
                in.fail("invalid cell");
            }
            cell.removedTop = static_cast<std::int32_t>(top);
        }
        if (cell.addedColumns > 0) {
            cell.addedTop = unzigzag(in.varint());
        }
        summary.cells.emplace(static_cast<std::uint16_t>(index), cell);
        previous = index;
    }
    in.expectEnd("trailing bytes");
    return summary;
}

} // namespace terrain
