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

// The sample (i, j, k) is an even sample of a stitched side: the coarser neighbor samples it.
bool coarseSample(int i, int j, int k, unsigned seams) {
    if (j % 2 != 0) {
        return false;
    }
    const bool lowX = i == 0;
    const bool highX = i == kLast;
    const bool lowZ = k == 0;
    const bool highZ = k == kLast;
    const bool evenX = i % 2 == 0;
    const bool evenZ = k % 2 == 0;
    const auto has = [seams](unsigned bit) { return (seams & bit) != 0; };
    return (has(kSeamNegX) && lowX && evenZ) || (has(kSeamPosX) && highX && evenZ) ||
           (has(kSeamNegZ) && lowZ && evenX) || (has(kSeamPosZ) && highZ && evenX) ||
           (has(kSeamNegXNegZ) && lowX && lowZ) || (has(kSeamPosXNegZ) && highX && lowZ) ||
           (has(kSeamNegXPosZ) && lowX && highZ) || (has(kSeamPosXPosZ) && highX && highZ);
}

// The direction of the air of the edits at a changed sample, seen from the vertex v (in cells):
// at LOD 0 up the smoothed share of the removed blocks, or down that of the added ones (the
// density rises away from them); beyond, toward the face of the nearest box of the relay that
// set the sample, on a stitched side that of the coarser LOD.
std::array<float, 3> editDirection(const LevelEdits& edits, std::size_t at, int i, int j, int k,
                                   const Vec3f& v) {
    const bool raised = edits.changes[at] > 0;
    if (edits.blocks) {
        const auto [removed, added] = edits.blocks->blocksAround(i, j, k);
        const std::array<float, 3> g = edit_cubes::shareGradient(raised ? removed : added);
        return raised ? g : std::array<float, 3>{-g[0], -g[1], -g[2]};
    }
    const std::array<double, 3> p{static_cast<double>(v.x), static_cast<double>(v.y),
                                  static_cast<double>(v.z)};
    if (edits.sides && coarseSample(i, j, k, edits.seams)) {
        return edits.sides->relayDirection(raised, i / 2, j / 2, k / 2,
                                           {p[0] / 2.0, p[1] / 2.0, p[2] / 2.0});
    }
    return edits.coarse ? edits.coarse->relayDirection(raised, i, j, k, p)
                        : std::array<float, 3>{};
}

// A sample whose change turns the normals: any change at LOD 0, a relay beyond.
bool turnsNormals(const LevelEdits& edits, std::size_t at) {
    const std::int8_t change = edits.changes[at];
    return edits.blocks ? change != LevelEdits::kKept
                        : change == LevelEdits::kRelayRaised ||
                              change == LevelEdits::kRelayLowered;
}

Vec3f normalized(float x, float y, float z, bool& ok) {
    const float length = std::sqrt(x * x + y * y + z * z);
    ok = length > 1e-6f;
    return ok ? Vec3f{x / length, y / length, z / length} : Vec3f{};
}

} // namespace

int editReach(int lod, unsigned seams) {
    const auto spacing = static_cast<int>(tileCellMeters(lod));
    const int own =
        lod == 0 ? kBlockReach : CoarseEdits::reach(spacing, CoarseEdits::relayOfLod(lod));
    if (seams == 0 || lod >= kMaxLod) {
        return own;
    }
    return std::max(own, CoarseEdits::reach(2 * spacing, CoarseEdits::relayOfLod(lod + 1)));
}

std::vector<int> levelsToMesh(const TileColumns& columns, const EditWindow& edits, int height,
                              double caveDepth, int reach) {
    std::set<int> levels;
    addLevels(levels, columns.minHeight - caveDepth, columns.maxHeight, height);
    // The edited blocks of a section span its edited heights: their levels are those of its
    // lowest and highest blocks and every level between, as block by block.
    for (const auto& [low, high] : edits.editedSpans()) {
        addLevels(levels, low - reach, high + reach, height);
    }
    return {levels.begin(), levels.end()};
}

bool filledLevel(const EditWindow& edits, const TileLattice& lattice, const TileColumns& columns,
                 int originY, int reach) {
    const int side = lattice.levelHeight;
    const BlockCoord min{lattice.x0 - reach, originY - reach, lattice.z0 - reach};
    const BlockCoord max{lattice.x0 + side + reach, originY + side + reach,
                         lattice.z0 + side + reach};
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
                                      const EditWindow& edits, int lod, unsigned seams) {
    const int s = lattice.spacing;
    std::optional<EditLattice> fine;
    std::optional<CoarseEdits> coarse;
    std::optional<CoarseEdits> sides;
    if (lod == 0) {
        fine.emplace(edits, lattice.x0, originY, lattice.z0, s, kGridSamples);
    } else {
        coarse.emplace(edits, lattice.x0, originY, lattice.z0, s,
                       std::array<int, 3>{kGridSamples, kGridSamples, kGridSamples},
                       CoarseEdits::relayOfLod(lod));
    }
    if (seams != 0 && lod < kMaxLod) {
        constexpr int kCoarse = kGridSamples / 2 + 1;
        sides.emplace(edits, lattice.x0, originY, lattice.z0, 2 * s,
                      std::array<int, 3>{kCoarse, kCoarse, kCoarse},
                      CoarseEdits::relayOfLod(lod + 1));
    }
    const bool own = fine ? !fine->empty() : !coarse->empty();
    if (!own && (!sides || sides->empty())) {
        return std::nullopt;
    }
    std::vector<std::int8_t> changes(static_cast<std::size_t>(kGridSamples) * kGridSamples *
                                         kGridSamples,
                                     LevelEdits::kKept);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int j = 0; j < kGridSamples; ++j) {
            for (int i = 0; i < kGridSamples; ++i) {
                const float generated = grid.at(i, j, k);
                bool relayed = false;
                float edited = fine ? fine->density(generated, i, j, k)
                                    : coarse->density(generated, i, j, k, relayed);
                if (sides && coarseSample(i, j, k, seams)) {
                    edited = sides->density(generated, i / 2, j / 2, k / 2, relayed);
                }
                grid.set(i, j, k, edited);
                if (edited != generated) {
                    changes[sampleAt(i, j, k)] =
                        static_cast<std::int8_t>((edited > generated ? 1 : -1) * (relayed ? 2 : 1));
                }
            }
        }
    }
    if (fine && fine->empty()) {
        fine.reset();
    }
    return LevelEdits{std::move(fine), std::move(coarse), std::move(sides), std::move(changes), s,
                      seams};
}

void lightEdits(Mesh& level, const LevelEdits& edits) {
    if (!edits.blocks && !edits.coarse && !edits.sides) {
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
        const bool lowChanged = turnsNormals(edits, a);
        const bool highChanged = edge.axis >= 0 && turnsNormals(edits, b);
        if (!lowChanged && !highChanged) {
            continue;
        }
        // The share of the edge on the side of the changed samples, and their direction there.
        const float t = edge.axis >= 0 ? edge.t : 0.0f;
        std::array<float, 3> sum{};
        float weight = 0.0f;
        const auto add = [&](std::size_t at, const std::array<int, 3>& p, float share) {
            const std::array<float, 3> d =
                editDirection(edits, at, p[0], p[1], p[2], level.vertices[n]);
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

void addEditWeights(const Mesh& level, const std::optional<LevelEdits>& edits,
                    std::size_t vertices, std::vector<float>& weights) {
    if (!edits) {
        if (!weights.empty()) {
            weights.resize(weights.size() + level.vertices.size(), 0.0f);
        }
        return;
    }
    weights.resize(vertices, 0.0f); // the levels before without edits
    weights.reserve(vertices + level.vertices.size());
    for (const Vec3f& vertex : level.vertices) {
        const VertexEdge edge = edgeOf(vertex);
        const bool low = edits->changes[sampleAt(edge.low[0], edge.low[1], edge.low[2])] !=
                         LevelEdits::kKept;
        float weight = low ? 1.0f - (edge.axis >= 0 ? edge.t : 0.0f) : 0.0f;
        if (edge.axis >= 0) {
            std::array<int, 3> high = edge.low;
            ++high[static_cast<std::size_t>(edge.axis)];
            if (edits->changes[sampleAt(high[0], high[1], high[2])] != LevelEdits::kKept) {
                weight += edge.t;
            }
        }
        weights.push_back(std::clamp(weight, 0.0f, 1.0f));
    }
}

} // namespace terrain::tile
