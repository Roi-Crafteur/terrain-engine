#include "horizon_mesh.hpp"

#include "terrain/generation/heightfield.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace terrain::unreal {

namespace {

// The samples of the window of a placed level, read from its toroidal buffer: sample (i, k) from
// the low corner lies in slot (first + (i, k)) mod side.
class WindowReader {
public:
    explicit WindowReader(const HorizonLevel& level)
        : slots_(level.slots()), side_(level.side()), first_(level.firstSlot()) {}

    [[nodiscard]] const HorizonCell& at(int i, int k) const {
        std::int64_t x = first_.x + i;
        std::int64_t z = first_.z + k;
        x -= x >= side_ ? side_ : 0;
        z -= z >= side_ ? side_ : 0;
        return slots_[static_cast<std::size_t>(z * side_ + x)];
    }

private:
    std::span<const HorizonCell> slots_;
    std::int64_t side_;
    HorizonIndex first_;
};

bool strictlyInside(const CellRect& rect, int i, int k) {
    return i > rect.i0 && i < rect.i1 && k > rect.k0 && k < rect.k1;
}

// The two triangles of a cell whose corners are (i, k), (i + 1, k), (i, k + 1), (i + 1, k + 1),
// counter-clockwise seen from above in the core: (b - a) x (c - a) points up.
void addCell(std::vector<std::int32_t>& triangles, std::int32_t v00, std::int32_t v10,
             std::int32_t v01, std::int32_t v11) {
    triangles.insert(triangles.end(), {v00, v01, v10, v10, v01, v11});
}

// The grid vertices along the border of the window: along x at k = 0, along z at i = K, back
// along x at k = K, back along z at i = 0; each corner once.
std::vector<std::int32_t> borderWalk(int cells, const std::vector<std::int32_t>& index) {
    const int side = cells + 1;
    const auto at = [&](int i, int k) { return index[static_cast<std::size_t>(k * side + i)]; };
    std::vector<std::int32_t> border;
    border.reserve(static_cast<std::size_t>(4 * cells));
    for (int i = 0; i < cells; ++i) {
        border.push_back(at(i, 0));
    }
    for (int k = 0; k < cells; ++k) {
        border.push_back(at(cells, k));
    }
    for (int i = cells; i > 0; --i) {
        border.push_back(at(i, cells));
    }
    for (int k = cells; k > 0; --k) {
        border.push_back(at(0, k));
    }
    return border;
}

// The skirt: below each edge (a, b) of the border walk, the quad down to the skirt vertices
// (a', b'), facing the inside of the window: (a, a', b) and (b, a', b').
void addSkirt(BodyTopology& topology) {
    const auto count = static_cast<std::int32_t>(topology.border.size());
    for (std::int32_t j = 0; j < count; ++j) {
        const std::int32_t next = (j + 1) % count;
        const std::int32_t a = topology.border[static_cast<std::size_t>(j)];
        const std::int32_t b = topology.border[static_cast<std::size_t>(next)];
        const std::int32_t below = topology.skirtFirst + j;
        const std::int32_t belowNext = topology.skirtFirst + next;
        topology.triangles.insert(topology.triangles.end(), {a, below, b, b, below, belowNext});
    }
}

void validateTopology(int cells, const CellRect& hole) {
    if (cells < 2 || cells % 2 != 0) {
        throw std::invalid_argument("bodyTopology: an even number of cells expected");
    }
    if (!hole.empty() && !CellRect{1, 1, cells - 1, cells - 1}.contains(hole)) {
        throw std::invalid_argument("bodyTopology: the hole must lie inside the window");
    }
}

// Each even sample of the border walk takes the altitude of the coarser sample there, then each
// odd one the mean altitude of its two neighbors: K is even, so the parity along the walk is the
// one along an edge, and the window starts on an even sample (its center is even, K / 2 too).
void stitchBorder(const HorizonLevel& level, const HorizonLevel& coarser,
                  const BodyTopology& topology, const HorizonLook& look,
                  std::vector<UnrealVector3f>& positions) {
    const std::size_t count = topology.border.size();
    const auto vertex = [&](std::size_t j) -> UnrealVector3f& {
        return positions[static_cast<std::size_t>(topology.border[j % count])];
    };
    const int side = topology.cells + 1;
    const HorizonRect window = level.window();
    for (std::size_t j = 0; j < count; j += 2) {
        const std::int32_t sample = topology.samples[static_cast<std::size_t>(topology.border[j])];
        const HorizonCell* cell = coarser.find(
            {(window.x0 + sample % side) / 2, (window.z0 + sample / side) / 2});
        if (cell == nullptr) {
            throw std::invalid_argument("fillBody: the coarser level does not surround this one");
        }
        vertex(j).z = horizonVertex(*cell, look).z;
    }
    for (std::size_t j = 1; j < count; j += 2) {
        vertex(j).z = 0.5f * (vertex(j - 1).z + vertex(j + 1).z);
    }
}

// The skirt, the sea square and the bounds vertex of a filled grid.
void fillExtras(const BodyTopology& topology, const BodyShape& shape, double pitchCm,
                UnrealMesh& mesh) {
    const auto skirtCm = static_cast<float>(shape.skirtMeters * kCentimetersPerMeter);
    for (std::size_t j = 0; j < topology.border.size(); ++j) {
        const auto top = static_cast<std::size_t>(topology.border[j]);
        const auto below = static_cast<std::size_t>(topology.skirtFirst) + j;
        mesh.positions[below] = mesh.positions[top];
        mesh.positions[below].z -= skirtCm;
        mesh.normals[below] = mesh.normals[top];
        mesh.colors[below] = mesh.colors[top];
    }
    const double center = topology.cells / 2 * pitchCm;
    const UnrealVector3f up{0.0f, 0.0f, 1.0f};
    if (topology.seaFirst >= 0) {
        const double half = shape.seaHalfMeters * kCentimetersPerMeter;
        const auto sea = static_cast<float>(kSeaLevel * kCentimetersPerMeter);
        const auto low = static_cast<float>(center - half);
        const auto high = static_cast<float>(center + half);
        const std::array<UnrealVector3f, 4> corners{
            {{low, low, sea}, {high, low, sea}, {low, high, sea}, {high, high, sea}}};
        for (std::size_t c = 0; c < 4; ++c) {
            const std::size_t v = static_cast<std::size_t>(topology.seaFirst) + c;
            mesh.positions[v] = corners[c];
            mesh.normals[v] = up;
            mesh.colors[v] = kSeaColor;
        }
    }
    float lowest = mesh.positions.front().z;
    for (std::size_t v = 0; v < topology.vertexCount() - 1; ++v) {
        lowest = std::min(lowest, mesh.positions[v].z);
    }
    const auto bounds = static_cast<std::size_t>(topology.boundsVertex);
    mesh.positions[bounds] = {static_cast<float>(center), static_cast<float>(center),
                              lowest - static_cast<float>(shape.curveMeters * kCentimetersPerMeter)};
    mesh.normals[bounds] = up;
    mesh.colors[bounds] = kSeaColor;
}

} // namespace

HorizonVertex horizonVertex(const HorizonCell& cell, const HorizonLook& look) {
    const bool ocean = look.sea && cell.height < kSeaLevel;
    const double ground = ocean ? static_cast<double>(kSeaLevel) : static_cast<double>(cell.height);
    HorizonVertex vertex;
    vertex.z = static_cast<float>((ground - look.dropMeters) * kCentimetersPerMeter);
    if (ocean) {
        vertex.normal = {0.0f, 0.0f, 1.0f};
        vertex.color = kSeaColor;
        return vertex;
    }
    // The core normal normalize(-gradX, 1, -gradZ), its axes swapped for Unreal.
    const double gx = cell.gradX;
    const double gz = cell.gradZ;
    const double length = std::sqrt(gx * gx + gz * gz + 1.0);
    vertex.normal = toUnrealDirection({static_cast<float>(-gx / length),
                                       static_cast<float>(1.0 / length),
                                       static_cast<float>(-gz / length)});
    vertex.color = altitudeColor(cell.height);
    return vertex;
}

BodyTopology bodyTopology(int cells, const CellRect& hole, bool seaSquare) {
    validateTopology(cells, hole);
    BodyTopology topology;
    topology.cells = cells;
    topology.hole = hole;
    const int side = cells + 1;
    std::vector<std::int32_t> index(static_cast<std::size_t>(side) * static_cast<std::size_t>(side),
                                    -1);
    for (int k = 0; k <= cells; ++k) {
        for (int i = 0; i <= cells; ++i) {
            if (!strictlyInside(hole, i, k)) {
                index[static_cast<std::size_t>(k * side + i)] =
                    static_cast<std::int32_t>(topology.samples.size());
                topology.samples.push_back(i + k * side);
            }
        }
    }
    const auto at = [&](int i, int k) { return index[static_cast<std::size_t>(k * side + i)]; };
    for (int k = 0; k < cells; ++k) {
        for (int i = 0; i < cells; ++i) {
            if (!hole.contains(i, k)) {
                addCell(topology.triangles, at(i, k), at(i + 1, k), at(i, k + 1), at(i + 1, k + 1));
            }
        }
    }
    topology.border = borderWalk(cells, index);
    topology.skirtFirst = static_cast<std::int32_t>(topology.samples.size());
    addSkirt(topology);
    std::int32_t next = topology.skirtFirst + static_cast<std::int32_t>(topology.border.size());
    if (seaSquare) {
        topology.seaFirst = next;
        addCell(topology.triangles, next, next + 1, next + 2, next + 3);
        next += 4;
    }
    topology.boundsVertex = next;
    return topology;
}

double skirtMeters(int pitch) {
    return std::max(2.0, pitch / 16.0);
}

void fillBody(const HorizonLevel& level, const BodyTopology& topology, const BodyShape& shape,
              const HorizonLook& look, UnrealMesh& mesh) {
    if (!level.placed() || level.side() != topology.cells + 1) {
        throw std::invalid_argument("fillBody: a placed level of the size of the topology expected");
    }
    const std::size_t count = topology.vertexCount();
    mesh.positions.resize(count);
    mesh.normals.resize(count);
    mesh.colors.resize(count);
    const WindowReader reader(level);
    const int side = topology.cells + 1;
    const double pitchCm = level.pitch() * kCentimetersPerMeter;
    for (std::size_t v = 0; v < topology.samples.size(); ++v) {
        const int i = topology.samples[v] % side;
        const int k = topology.samples[v] / side;
        const HorizonVertex vertex = horizonVertex(reader.at(i, k), look);
        mesh.positions[v] = {static_cast<float>(i * pitchCm), static_cast<float>(k * pitchCm),
                             vertex.z};
        mesh.normals[v] = vertex.normal;
        mesh.colors[v] = vertex.color;
    }
    if (shape.coarser != nullptr) {
        stitchBorder(level, *shape.coarser, topology, look, mesh.positions);
    }
    fillExtras(topology, shape, pitchCm, mesh);
}

UnrealMesh levelTrim(const HorizonLevel& level, const CellRect& square, const CellRect& finer,
                     const HorizonLook& look) {
    const int cells = level.side() - 1;
    if (!level.placed() || !CellRect{0, 0, cells, cells}.contains(square) ||
        !square.contains(finer)) {
        throw std::invalid_argument("levelTrim: the finer window must lie inside the square");
    }
    const WindowReader reader(level);
    const double pitchCm = level.pitch() * kCentimetersPerMeter;
    const int width = square.i1 - square.i0 + 1;
    std::vector<std::int32_t> index(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(square.k1 - square.k0 + 1), -1);
    UnrealMesh mesh;
    const auto vertexAt = [&](int i, int k) {
        std::int32_t& slot = index[static_cast<std::size_t>((k - square.k0) * width + i - square.i0)];
        if (slot < 0) {
            slot = static_cast<std::int32_t>(mesh.positions.size());
            const HorizonVertex vertex = horizonVertex(reader.at(i, k), look);
            mesh.positions.push_back(
                {static_cast<float>(i * pitchCm), static_cast<float>(k * pitchCm), vertex.z});
            mesh.normals.push_back(vertex.normal);
            mesh.colors.push_back(vertex.color);
        }
        return slot;
    };
    for (int k = square.k0; k < square.k1; ++k) {
        for (int i = square.i0; i < square.i1; ++i) {
            if (!finer.contains(i, k)) {
                const std::int32_t v00 = vertexAt(i, k);
                const std::int32_t v10 = vertexAt(i + 1, k);
                const std::int32_t v01 = vertexAt(i, k + 1);
                addCell(mesh.triangles, v00, v10, v01, vertexAt(i + 1, k + 1));
            }
        }
    }
    return mesh;
}

} // namespace terrain::unreal
