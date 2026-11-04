#include "tile_edits.hpp"

#include "../storage/edit_cubes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <set>
#include <utility>

namespace terrain::tile {

namespace {

constexpr int kLast = kGridSamples - 1;

std::size_t sampleAt(int i, int j, int k) {
    return static_cast<std::size_t>((k * kGridSamples + j) * kGridSamples + i);
}

// The edge of the grid a vertex of a level lies on (in cells): two of its coordinates are whole;
// a vertex on a sample has none moving (axis -1).
struct VertexEdge {
    std::array<int, 3> low{};
    int axis = -1;
    float t = 0.0f;
};

VertexEdge edgeOf(const Vec3f& v) {
    const std::array<float, 3> p{v.x, v.y, v.z};
    VertexEdge edge;
    for (int a = 0; a < 3; ++a) {
        const auto u = static_cast<std::size_t>(a);
        const float whole = std::round(p[u]);
        edge.low[u] = std::clamp(static_cast<int>(whole), 0, kLast);
        if (std::abs(p[u] - whole) > 1e-4f && edge.axis < 0) {
            edge.axis = a;
            edge.low[u] = std::clamp(static_cast<int>(std::floor(p[u])), 0, kLast - 1);
            edge.t = p[u] - static_cast<float>(edge.low[u]);
        }
    }
    return edge;
}

// The direction of the air of the edits at a changed sample: up the smoothed share of the
// removed blocks, or down that of the added ones (the density rises away from them).
std::array<float, 3> editDirection(const LevelEdits& edits, std::size_t at, int i, int j, int k) {
    const auto [removed, added] = edits.blocks.blocksAround(i, j, k);
    const bool raised = edits.changes[at] == LevelEdits::kRaised;
    const std::array<float, 3> g = edit_cubes::shareGradient(raised ? removed : added);
    return raised ? g : std::array<float, 3>{-g[0], -g[1], -g[2]};
}

Vec3f normalized(float x, float y, float z, bool& ok) {
    const float length = std::sqrt(x * x + y * y + z * z);
    ok = length > 1e-6f;
    return ok ? Vec3f{x / length, y / length, z / length} : Vec3f{};
}

} // namespace

std::vector<int> levelsToMesh(const TileColumns& columns, const EditWindow& edits, int height,
                              double caveDepth) {
    std::set<int> levels;
    addLevels(levels, columns.minHeight - caveDepth, columns.maxHeight, height);
    // The edited blocks of a section span its edited heights: their levels are those of its
    // lowest and highest blocks and every level between, as block by block.
    for (const auto& [low, high] : edits.editedSpans()) {
        addLevels(levels, low - kBlockReach, high + kBlockReach, height);
    }
    return {levels.begin(), levels.end()};
}

bool filledLevel(const EditWindow& edits, const TileLattice& lattice, const TileColumns& columns,
                 int originY) {
    const int side = lattice.levelHeight;
    const BlockCoord min{lattice.x0 - kBlockReach, originY - kBlockReach,
                         lattice.z0 - kBlockReach};
    const BlockCoord max{lattice.x0 + side + kBlockReach, originY + side + kBlockReach,
                         lattice.z0 + side + kBlockReach};
    if (static_cast<double>(originY) > columns.maxHeight &&
        !edits.holds(BlockEdit::Added, min, max)) {
        return true;
    }
    if (static_cast<double>(originY + side) < columns.minHeight &&
        !edits.holds(BlockEdit::Removed, min, max)) {
        return true;
    }
    return edits.filled(min, max);
}

std::optional<LevelEdits> applyBlocks(DensityGrid& grid, const TileLattice& lattice, int originY,
                                      const EditWindow& edits) {
    EditLattice samples(edits, lattice.x0, originY, lattice.z0, lattice.spacing, kGridSamples);
    if (samples.empty()) {
        return std::nullopt;
    }
    std::vector<std::int8_t> changes(static_cast<std::size_t>(kGridSamples) * kGridSamples *
                                         kGridSamples,
                                     LevelEdits::kKept);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int j = 0; j < kGridSamples; ++j) {
            for (int i = 0; i < kGridSamples; ++i) {
                const float generated = grid.at(i, j, k);
                const float edited = samples.density(generated, i, j, k);
                grid.set(i, j, k, edited);
                if (edited != generated) {
                    changes[sampleAt(i, j, k)] =
                        edited > generated ? LevelEdits::kRaised : LevelEdits::kLowered;
                }
            }
        }
    }
    return LevelEdits{std::move(samples), std::move(changes), lattice.spacing};
}

void lightEdits(Mesh& level, const LevelEdits& edits) {
    if (edits.spacing != 1) {
        return;
    }
    for (std::size_t n = 0; n < level.vertices.size(); ++n) {
        const VertexEdge edge = edgeOf(level.vertices[n]);
        std::array<int, 3> high = edge.low;
        if (edge.axis >= 0) {
            ++high[static_cast<std::size_t>(edge.axis)];
        }
        const std::size_t a = sampleAt(edge.low[0], edge.low[1], edge.low[2]);
        const std::size_t b = sampleAt(high[0], high[1], high[2]);
        const bool lowChanged = edits.changes[a] != LevelEdits::kKept;
        const bool highChanged = edge.axis >= 0 && edits.changes[b] != LevelEdits::kKept;
        if (!lowChanged && !highChanged) {
            continue;
        }
        // The share of the edge on the side of the changed samples, and their direction there.
        const float t = edge.axis >= 0 ? edge.t : 0.0f;
        std::array<float, 3> sum{};
        float weight = 0.0f;
        const auto add = [&](std::size_t at, const std::array<int, 3>& p, float share) {
            const std::array<float, 3> d = editDirection(edits, at, p[0], p[1], p[2]);
            for (std::size_t u = 0; u < 3; ++u) {
                sum[u] += share * d[u];
            }
            weight += share;
        };
        if (lowChanged) {
            add(a, edge.low, 1.0f - t);
        }
        if (highChanged) {
            add(b, high, t);
        }
        bool ok = false;
        const Vec3f toward = normalized(sum[0], sum[1], sum[2], ok);
        if (!ok) {
            continue; // the blocks all around: no direction, the normal of the grid kept
        }
        const Vec3f& old = level.normals[n];
        const float keep = 1.0f - std::min(weight, 1.0f);
        bool blended = false;
        const Vec3f normal = normalized(keep * old.x + (1.0f - keep) * toward.x,
                                        keep * old.y + (1.0f - keep) * toward.y,
                                        keep * old.z + (1.0f - keep) * toward.z, blended);
        level.normals[n] = blended ? normal : toward;
    }
}

} // namespace terrain::tile
