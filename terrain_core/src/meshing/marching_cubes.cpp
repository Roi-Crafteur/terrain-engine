#include "terrain/meshing/marching_cubes.hpp"

#include "marching_cubes_tables.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace terrain {

namespace {

// Rejects at compile time any triangle table row that is malformed or references an
// edge the surface does not cross in that case.
constexpr bool triangleTableMatchesEdgeTable() {
    for (std::size_t cube = 0; cube < mc::kTriangleTable.size(); ++cube) {
        const auto& row = mc::kTriangleTable[cube];
        unsigned usedEdges = 0;
        std::size_t count = 0;
        for (; count < row.size() && row[count] >= 0; ++count) {
            if (row[count] >= 12) {
                return false;
            }
            usedEdges |= 1U << row[count];
        }
        if (count % 3 != 0 || count == row.size() || usedEdges != mc::kEdgeTable[cube]) {
            return false;
        }
    }
    return true;
}

static_assert(triangleTableMatchesEdgeTable(), "Marching Cubes triangle table is inconsistent");

constexpr std::uint32_t kNoVertex = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t kSamplesPerAxis = kGridSamples;
constexpr std::size_t kSampleCount = kSamplesPerAxis * kSamplesPerAxis * kSamplesPerAxis;

struct GridPoint {
    int x = 0;
    int y = 0;
    int z = 0;
};

std::size_t sampleIndex(GridPoint p) {
    return static_cast<std::size_t>((p.z * kGridSamples + p.y) * kGridSamples + p.x);
}

Vec3f lerp(Vec3f a, Vec3f b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

Vec3f toMeters(GridPoint p) {
    constexpr auto kCell = static_cast<float>(kCellSizeMeters);
    return {static_cast<float>(p.x) * kCell, static_cast<float>(p.y) * kCell,
            static_cast<float>(p.z) * kCell};
}

// Builds the mesh cell by cell, creating each edge vertex once and sharing it afterwards.
class MeshBuilder {
public:
    explicit MeshBuilder(const DensityGrid& grid)
        : grid_(grid), edgeVertices_(3 * kSampleCount, kNoVertex) {}

    void polygonizeCell(GridPoint cell);
    Mesh take() { return std::move(mesh_); }

private:
    [[nodiscard]] float density(GridPoint p) const { return grid_.at(p.x, p.y, p.z); }
    [[nodiscard]] float derivative(GridPoint p, int axis) const;
    [[nodiscard]] Vec3f gradient(GridPoint p) const;
    std::uint32_t edgeVertex(GridPoint a, GridPoint b);
    [[nodiscard]] Vec3f edgeNormal(GridPoint low, GridPoint high, float t) const;

    const DensityGrid& grid_;
    std::vector<std::uint32_t> edgeVertices_; // per (axis, lower sample) grid edge
    Mesh mesh_;
};

void MeshBuilder::polygonizeCell(GridPoint cell) {
    std::array<GridPoint, 8> corners{};
    unsigned cube = 0;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const mc::CornerOffset offset = mc::kCornerOffsets[i];
        const int x = cell.x + offset.x;
        const int y = cell.y + offset.y;
        const int z = cell.z + offset.z;
        corners[i] = {x, y, z};
        // From the coordinates, not from corners[i] just stored: Clang reloads its x and y as
        // one 8-byte word, which the processor cannot forward from their two 4-byte stores, a
        // stall at every corner of every cell.
        if (grid_.at(x, y, z) < kIsoLevel) {
            cube |= 1U << i;
        }
    }
    const unsigned crossedEdges = mc::kEdgeTable[cube];
    if (crossedEdges == 0) {
        return;
    }

    std::array<std::uint32_t, 12> vertexOnEdge{};
    for (std::size_t edge = 0; edge < vertexOnEdge.size(); ++edge) {
        if ((crossedEdges & (1U << edge)) != 0) {
            const auto [a, b] = mc::kEdgeCorners[edge];
            vertexOnEdge[edge] = edgeVertex(corners[static_cast<std::size_t>(a)],
                                            corners[static_cast<std::size_t>(b)]);
        }
    }
    // With corners numbered as in kCornerOffsets, Bourke's triangles are counter-clockwise
    // seen from the air, so they are emitted in table order.
    const auto& row = mc::kTriangleTable[cube];
    for (std::size_t i = 0; row[i] >= 0; ++i) {
        mesh_.indices.push_back(vertexOnEdge[static_cast<std::size_t>(row[i])]);
    }
}

// Central difference inside the grid, one-sided on its faces.
float MeshBuilder::derivative(GridPoint p, int axis) const {
    GridPoint before = p;
    GridPoint after = p;
    int& beforeCoord = axis == 0 ? before.x : (axis == 1 ? before.y : before.z);
    int& afterCoord = axis == 0 ? after.x : (axis == 1 ? after.y : after.z);
    beforeCoord = std::max(beforeCoord - 1, 0);
    afterCoord = std::min(afterCoord + 1, kGridSamples - 1);
    return (density(after) - density(before)) / static_cast<float>(afterCoord - beforeCoord);
}

Vec3f MeshBuilder::gradient(GridPoint p) const {
    return {derivative(p, 0), derivative(p, 1), derivative(p, 2)};
}

Vec3f MeshBuilder::edgeNormal(GridPoint low, GridPoint high, float t) const {
    const Vec3f g = lerp(gradient(low), gradient(high), t);
    const float length = std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
    if (length > std::numeric_limits<float>::min()) {
        return {g.x / length, g.y / length, g.z / length};
    }
    // Flat gradient: fall back to the edge direction, from its solid end to its air end.
    const float sign = density(low) < kIsoLevel ? 1.0f : -1.0f;
    return {static_cast<float>(high.x - low.x) * sign, static_cast<float>(high.y - low.y) * sign,
            static_cast<float>(high.z - low.z) * sign};
}

// Returns the vertex where the surface crosses the grid edge (a, b), creating it once.
std::uint32_t MeshBuilder::edgeVertex(GridPoint a, GridPoint b) {
    // Interpolate from the lower sample so the result never depends on the calling cell.
    const GridPoint low{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    const GridPoint high{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    const std::size_t axis = high.x != low.x ? 0 : (high.y != low.y ? 1 : 2);
    std::uint32_t& vertex = edgeVertices_[axis * kSampleCount + sampleIndex(low)];
    if (vertex != kNoVertex) {
        return vertex;
    }

    const float dLow = density(low);
    const float dHigh = density(high);
    const float t = std::clamp((kIsoLevel - dLow) / (dHigh - dLow), 0.0f, 1.0f);
    vertex = static_cast<std::uint32_t>(mesh_.vertices.size());
    mesh_.vertices.push_back(lerp(toMeters(low), toMeters(high), t));
    mesh_.normals.push_back(edgeNormal(low, high, t));
    return vertex;
}

} // namespace

Mesh marchingCubes(const DensityGrid& grid) {
    MeshBuilder builder(grid);
    for (int z = 0; z < kChunkCells; ++z) {
        for (int y = 0; y < kChunkCells; ++y) {
            for (int x = 0; x < kChunkCells; ++x) {
                builder.polygonizeCell({x, y, z});
            }
        }
    }
    return builder.take();
}

} // namespace terrain
