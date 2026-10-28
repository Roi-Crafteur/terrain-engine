#include "terrain/streaming/horizon_edit_columns.hpp"

#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/storage/modifications.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <utility>

namespace terrain {

namespace {

constexpr int kSpacing = kEditColumnSpacing;
constexpr int kGroupChunks = 32;    // the chunks of a region of the world format, per side
constexpr int kColumnsPerChunk = 2; // kChunkSizeMeters / kSpacing

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return q * b > a ? q - 1 : q;
}

std::int64_t ceilDiv(std::int64_t a, std::int64_t b) {
    return -floorDiv(-a, b);
}

// Generated density of the lattice point y = 8 j of a column: the expression of the tiles
// (tile::levelDensity), for the same bits.
float generatedDensity(std::int64_t j, float ground) {
    return static_cast<float>(static_cast<double>(j * kSpacing) - ground);
}

// The density of a point among removed (added) blocks only: the cube of the block centered on it
// (blockDistance -0.5), as editDensity applies it.
constexpr float kAllRemoved = 0.5f;
constexpr float kAllAdded = -0.5f;

// The highest lattice point under the generated ground (rock: density below the iso level).
std::int64_t generatedTop(float ground) {
    std::int64_t top = static_cast<std::int64_t>(ground) / kSpacing;
    while (generatedDensity(top + 1, ground) < kIsoLevel) {
        ++top;
    }
    while (generatedDensity(top, ground) >= kIsoLevel) {
        --top;
    }
    return top;
}

using Group = std::pair<std::int64_t, std::int64_t>;
using ColumnSet = std::set<EditColumn>; // the columns of a group, in its image

// The columns of the lattice points of each chunk, by group of chunks.
std::map<Group, ColumnSet> columnsByGroup(std::span<const ChunkCoord> chunks,
                                          const WorldSize& world) {
    std::map<Group, ColumnSet> groups;
    for (const ChunkCoord chunk : chunks) {
        const ChunkCoord c = wrapChunk(chunk, world);
        ColumnSet& columns = groups[{floorDiv(c.x, kGroupChunks), floorDiv(c.z, kGroupChunks)}];
        for (int dz = 0; dz <= kColumnsPerChunk; ++dz) {
            for (int dx = 0; dx <= kColumnsPerChunk; ++dx) {
                columns.insert({std::int64_t{c.x} * kColumnsPerChunk + dx,
                                std::int64_t{c.z} * kColumnsPerChunk + dz});
            }
        }
    }
    return groups;
}

// The edited ground of the columns of a group, read from one window around them.
void addGroup(const Heightfield& field, const ColumnSet& columns, const EditWindowSource& windows,
              std::vector<EditedColumn>& out) {
    std::int64_t x0 = columns.begin()->x;
    std::int64_t x1 = x0;
    std::int64_t z0 = columns.begin()->z;
    std::int64_t z1 = z0;
    for (const EditColumn& column : columns) {
        x0 = std::min(x0, column.x);
        x1 = std::max(x1, column.x);
        z0 = std::min(z0, column.z);
        z1 = std::max(z1, column.z);
    }
    const EditWindow window = windows(static_cast<int>(x0 * kSpacing - kBlockReach),
                                      static_cast<int>(x1 * kSpacing + kBlockReach),
                                      static_cast<int>(z0 * kSpacing - kBlockReach),
                                      static_cast<int>(z1 * kSpacing + kBlockReach));
    // The heights of the columns near an edited block, in one batch.
    std::vector<EditColumn> near;
    std::vector<double> xs;
    std::vector<double> zs;
    const WorldSize& world = field.world();
    const auto countX = static_cast<std::int64_t>(world.x) / kSpacing;
    const auto countZ = static_cast<std::int64_t>(world.z) / kSpacing;
    for (const EditColumn& column : columns) {
        const auto x = static_cast<int>(column.x * kSpacing);
        const auto z = static_cast<int>(column.z * kSpacing);
        const EditColumn canonical{wrapIndex(column.x, countX), wrapIndex(column.z, countZ)};
        if (!window.editedSpan(x - kBlockReach, x + kBlockReach, z - kBlockReach,
                               z + kBlockReach)) {
            out.push_back({canonical, std::nullopt, 0.0f});
            continue;
        }
        near.push_back(column);
        xs.push_back(x);
        zs.push_back(z);
    }
    std::vector<float> grounds(near.size());
    field.heightsAt(xs, zs, grounds);
    for (std::size_t n = 0; n < near.size(); ++n) {
        const auto x = static_cast<int>(near[n].x * kSpacing);
        const auto z = static_cast<int>(near[n].z * kSpacing);
        const EditColumn canonical{wrapIndex(near[n].x, countX), wrapIndex(near[n].z, countZ)};
        const std::optional<float> edited = editedColumnGround(window, grounds[n], x, z);
        out.push_back({canonical, edited, edited ? grounds[n] : 0.0f});
    }
}

} // namespace

std::optional<float> editedColumnGround(const EditWindow& window, float ground, int x, int z) {
    const std::optional<std::pair<int, int>> span =
        window.editedSpan(x - kBlockReach, x + kBlockReach, z - kBlockReach, z + kBlockReach);
    if (!span) {
        return std::nullopt;
    }
    // The lattice points an edited block may reach (kBlockReach of its center).
    const std::int64_t low = ceilDiv(std::int64_t{span->first} - kBlockReach, kSpacing);
    const std::int64_t high = floorDiv(std::int64_t{span->second} + kBlockReach, kSpacing);
    const auto density = [&](std::int64_t j) {
        const float generated = generatedDensity(j, ground);
        if (j < low || j > high) {
            return generated;
        }
        const int y = static_cast<int>(j * kSpacing);
        // Within sections filled with one edit (a razed volume, a heap), the 27 blocks around
        // the point share it: EditWindow::density there, without reading them one by one.
        if (window.filled({x - kBlockReach, y - kBlockReach, z - kBlockReach},
                          {x + kBlockReach, y + kBlockReach, z + kBlockReach})) {
            return window.blockAt(x, y, z) == BlockEdit::Removed ? std::max(generated, kAllRemoved)
                                                                 : std::min(generated, kAllAdded);
        }
        return window.density(generated, x, y, z);
    };
    // From the air over the generated rock and over every edited point, down to the first rock:
    // under the lowest edited point, the generated rock at the latest.
    const std::int64_t top = generatedTop(ground);
    std::int64_t j = std::max(top, high);
    float above = density(j + 1);
    float below = density(j);
    while (below >= kIsoLevel) {
        above = below;
        below = density(--j);
    }
    if (j == top && below == generatedDensity(top, ground) &&
        above == generatedDensity(top + 1, ground)) {
        return std::nullopt;
    }
    // As marchingCubes on the edge from j (lower) to j + 1.
    const float t = std::clamp((kIsoLevel - below) / (above - below), 0.0f, 1.0f);
    return static_cast<float>(static_cast<double>(j * kSpacing) +
                              static_cast<double>(t) * kSpacing);
}

std::vector<EditedColumn> editedColumns(const Heightfield& field,
                                        std::span<const ChunkCoord> chunks,
                                        const EditWindowSource& windows) {
    std::vector<EditedColumn> out;
    for (const auto& [group, columns] : columnsByGroup(chunks, field.world())) {
        addGroup(field, columns, windows, out);
    }
    // The columns on the border of two groups, computed by both: the same values.
    std::sort(out.begin(), out.end(),
              [](const EditedColumn& a, const EditedColumn& b) { return a.column < b.column; });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const EditedColumn& a, const EditedColumn& b) {
                              return a.column == b.column;
                          }),
              out.end());
    return out;
}

} // namespace terrain
