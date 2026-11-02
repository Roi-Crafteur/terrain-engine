#include "terrain/io/mesh_checks.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

namespace terrain {

namespace {

// The bits of a coordinate, -0 as +0.
std::uint32_t bitsOf(float value) {
    value += 0.0f;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

struct PositionKey {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
    friend bool operator==(const PositionKey&, const PositionKey&) = default;
};

struct PositionHash {
    std::size_t operator()(const PositionKey& k) const {
        return (static_cast<std::size_t>(k.x) * 73'856'093U) ^
               (static_cast<std::size_t>(k.y) * 19'349'663U) ^
               (static_cast<std::size_t>(k.z) * 83'492'791U);
    }
};

// The welded index of each vertex.
std::vector<std::uint32_t> weld(const Mesh& mesh, std::vector<Vec3f>& positions) {
    std::unordered_map<PositionKey, std::uint32_t, PositionHash> welded;
    std::vector<std::uint32_t> ids(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3f& v = mesh.vertices[i];
        const auto [at, added] = welded.try_emplace(
            {bitsOf(v.x), bitsOf(v.y), bitsOf(v.z)}, static_cast<std::uint32_t>(positions.size()));
        if (added) {
            positions.push_back(v);
        }
        ids[i] = at->second;
    }
    return ids;
}

std::uint64_t edgeKey(std::uint32_t a, std::uint32_t b) {
    return (static_cast<std::uint64_t>(std::min(a, b)) << 32U) | std::max(a, b);
}

// Whether the triangle of indices first..first + 2 has its normals against its winding; false
// for a triangle without area.
bool invertedTriangle(const Mesh& mesh, std::size_t first, bool& degenerate) {
    const Vec3f& a = mesh.vertices[mesh.indices[first]];
    const Vec3f& b = mesh.vertices[mesh.indices[first + 1]];
    const Vec3f& c = mesh.vertices[mesh.indices[first + 2]];
    const std::array<double, 3> u{static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y,
                                  static_cast<double>(b.z) - a.z};
    const std::array<double, 3> v{static_cast<double>(c.x) - a.x, static_cast<double>(c.y) - a.y,
                                  static_cast<double>(c.z) - a.z};
    const std::array<double, 3> winding{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                        u[0] * v[1] - u[1] * v[0]};
    degenerate = !(winding[0] * winding[0] + winding[1] * winding[1] + winding[2] * winding[2] >
                   0.0);
    double toward = 0.0;
    for (std::size_t k = 0; k < 3; ++k) {
        const Vec3f& n = mesh.normals[mesh.indices[first + k]];
        toward += winding[0] * n.x + winding[1] * n.y + winding[2] * n.z;
    }
    return !degenerate && toward < 0.0;
}

} // namespace

MeshCheck checkTileMesh(const Mesh& mesh, float side) {
    MeshCheck out;
    std::vector<Vec3f> positions;
    const std::vector<std::uint32_t> ids = weld(mesh, positions);
    out.triangles = mesh.indices.size() / 3;
    out.marks.assign(out.triangles, kMarkSound);
    std::unordered_map<std::uint64_t, int> edges;
    for (std::size_t t = 0; t < out.triangles; ++t) {
        const std::array<std::uint32_t, 3> v{ids[mesh.indices[3 * t]], ids[mesh.indices[3 * t + 1]],
                                             ids[mesh.indices[3 * t + 2]]};
        bool flat = false;
        const bool inverted = invertedTriangle(mesh, 3 * t, flat);
        if (flat || v[0] == v[1] || v[1] == v[2] || v[0] == v[2]) {
            ++out.degenerate;
            continue;
        }
        if (inverted) {
            ++out.inverted;
            out.marks[t] = kMarkInverted;
        }
        for (std::size_t k = 0; k < 3; ++k) {
            ++edges[edgeKey(v[k], v[(k + 1) % 3])];
        }
    }
    const auto onOutline = [side](const Vec3f& p, const Vec3f& q) {
        return (p.x == 0.0f && q.x == 0.0f) || (p.x == side && q.x == side) ||
               (p.z == 0.0f && q.z == 0.0f) || (p.z == side && q.z == side);
    };
    std::unordered_map<std::uint64_t, bool> open;
    for (const auto& [key, count] : edges) {
        out.nonManifoldEdges += count > 2 ? 1 : 0;
        const Vec3f& p = positions[key >> 32U];
        const Vec3f& q = positions[key & 0xffff'ffffU];
        if (count == 1 && !onOutline(p, q)) {
            ++out.openEdges;
            open.emplace(key, true);
        }
    }
    for (std::size_t t = 0; t < out.triangles && !open.empty(); ++t) {
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t a = ids[mesh.indices[3 * t + k]];
            const std::uint32_t b = ids[mesh.indices[3 * t + (k + 1) % 3]];
            if (open.count(edgeKey(a, b)) != 0) {
                out.marks[t] = kMarkOpen;
            }
        }
    }
    return out;
}

} // namespace terrain
