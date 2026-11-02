#include "tile_normals.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain::tile {

namespace {

// The least cosine of a normal with the normal of its triangles.
constexpr float kAgree = 0.3f;
// A normal turned in front of a folded triangle faces it by kFront times the round (1, 2, ...)
// at most kFrontMost: the other vertices of a triangle face it more and more, until they
// outweigh a vertex its neighbors pull their way.
constexpr float kFront = 0.1f;
constexpr float kFrontMost = 0.5f;
constexpr int kRounds = 12;
// A vertex is torn where the normals of its triangles (of unit length) sum to less than kTorn of
// their count: triangles back to back on a fold as thin as a cell (a crest of rock sharper than
// 60 degrees), which would pull its normal each its way. It takes the normal whose least cosine
// with them is the greatest (approached in kFacing steps): it faces each of them a little.
constexpr float kTorn = 0.5f;
constexpr int kFacing = 32;

// The normal of a triangle of a mesh (indices first..first + 2), its length twice its area.
Vec3f windingOf(const Mesh& mesh, std::size_t first) {
    const Vec3f& a = mesh.vertices[mesh.indices[first]];
    const Vec3f& b = mesh.vertices[mesh.indices[first + 1]];
    const Vec3f& c = mesh.vertices[mesh.indices[first + 2]];
    const Vec3f u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3f v{c.x - a.x, c.y - a.y, c.z - a.z};
    return {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
}

float dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f unit(const Vec3f& v) {
    const float length = std::sqrt(dot(v, v));
    return length > 0.0f ? Vec3f{v.x / length, v.y / length, v.z / length} : v;
}

// The unit normal n turned toward the unit direction w until their cosine is cosine, in their
// plane: n + t w with (n.w + t) / |n + t w| = cosine.
Vec3f turnedToward(const Vec3f& n, const Vec3f& w, float cosine) {
    const float along = dot(n, w);
    const float across = std::sqrt(std::max(0.0f, 1.0f - along * along));
    if (!(across > 1e-6f)) {
        return w; // n along w or against it: no plane to turn in
    }
    const float t = cosine * across / std::sqrt(1.0f - cosine * cosine) - along;
    return unit({n.x + t * w.x, n.y + t * w.y, n.z + t * w.z});
}

// The normals of the triangles of a mesh summed at their vertices, weighted by their areas.
std::vector<Vec3f> triangleNormals(const Mesh& mesh) {
    std::vector<Vec3f> sums(mesh.vertices.size());
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const Vec3f n = windingOf(mesh, t);
        for (std::size_t k = 0; k < 3; ++k) {
            Vec3f& sum = sums[mesh.indices[t + k]];
            sum = {sum.x + n.x, sum.y + n.y, sum.z + n.z};
        }
    }
    return sums;
}

// Whether each vertex of a mesh is torn (kTorn).
std::vector<bool> tornVertices(const Mesh& mesh) {
    std::vector<Vec3f> sums(mesh.vertices.size());
    std::vector<float> counts(mesh.vertices.size());
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const Vec3f n = unit(windingOf(mesh, t));
        if (!(dot(n, n) > 0.0f)) {
            continue; // without area
        }
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t v = mesh.indices[t + k];
            sums[v] = {sums[v].x + n.x, sums[v].y + n.y, sums[v].z + n.z};
            counts[v] += 1.0f;
        }
    }
    std::vector<bool> torn(mesh.vertices.size());
    for (std::size_t v = 0; v < torn.size(); ++v) {
        torn[v] = std::sqrt(dot(sums[v], sums[v])) < kTorn * counts[v];
    }
    return torn;
}

// The normal of torn vertex v of a mesh: from the sum of the normals of its triangles (or its
// own, if they cancel out), turned toward the triangle it faces least by shorter and shorter
// steps.
Vec3f facingAll(const Mesh& mesh, std::uint32_t v) {
    std::vector<Vec3f> sides;
    Vec3f sum{};
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const Vec3f side = unit(windingOf(mesh, t));
        if ((mesh.indices[t] == v || mesh.indices[t + 1] == v || mesh.indices[t + 2] == v) &&
            dot(side, side) > 0.0f) {
            sides.push_back(side);
            sum = {sum.x + side.x, sum.y + side.y, sum.z + side.z};
        }
    }
    Vec3f n = dot(sum, sum) > 1e-6f ? unit(sum) : mesh.normals[v];
    for (int step = 0; step < kFacing && !sides.empty(); ++step) {
        const Vec3f least = *std::min_element(
            sides.begin(), sides.end(),
            [&n](const Vec3f& a, const Vec3f& b) { return dot(n, a) < dot(n, b); });
        const float share = 1.0f / static_cast<float>(step + 2);
        n = unit({n.x + share * least.x, n.y + share * least.y, n.z + share * least.z});
    }
    return n;
}

// Turns the normals of the vertices of the triangles lit against their winding in front of them
// (a torn vertex: once, facingAll), until none is (their neighbors share vertices with them) or
// kRounds times.
void unfold(Mesh& mesh) {
    std::vector<bool> torn;    // found at the first fold
    std::vector<bool> settled; // the torn vertices given their normal
    for (int round = 0; round < kRounds; ++round) {
        const float front = std::min(kFrontMost, kFront * static_cast<float>(round + 1));
        bool folded = false;
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const Vec3f winding = windingOf(mesh, t);
            Vec3f sum{};
            for (std::size_t k = 0; k < 3; ++k) {
                const Vec3f& n = mesh.normals[mesh.indices[t + k]];
                sum = {sum.x + n.x, sum.y + n.y, sum.z + n.z};
            }
            if (!(dot(sum, winding) < 0.0f)) {
                continue;
            }
            folded = true;
            if (torn.empty()) {
                torn = tornVertices(mesh);
                settled.assign(torn.size(), false);
            }
            const Vec3f w = unit(winding);
            for (std::size_t k = 0; k < 3; ++k) {
                const std::uint32_t v = mesh.indices[t + k];
                Vec3f& n = mesh.normals[v];
                if (torn[v]) {
                    n = settled[v] ? n : facingAll(mesh, v);
                    settled[v] = true;
                } else if (dot(n, w) < front) {
                    n = turnedToward(n, w, front);
                }
            }
        }
        if (!folded) {
            return;
        }
    }
}

} // namespace

void agreeWithTriangles(Mesh& mesh) {
    const std::vector<Vec3f> around = triangleNormals(mesh);
    for (std::size_t n = 0; n < mesh.vertices.size(); ++n) {
        const Vec3f a = unit(around[n]);
        if (dot(a, a) > 0.0f && dot(mesh.normals[n], a) < kAgree) {
            mesh.normals[n] = turnedToward(mesh.normals[n], a, kAgree);
        }
    }
    unfold(mesh);
}

} // namespace terrain::tile
