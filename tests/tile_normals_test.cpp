// The normals of the meshes of the streamed tiles against their triangles (phase 12A,
// src/streaming/tile_normals.hpp): a normal that agrees with its triangles keeps its bits; one
// that does not turns just far enough toward them (to a cosine of 0.3, in the plane of both), one
// opposite to them takes theirs; on a blade of rock as thin as a cell, whose two sides share the
// vertices of its crest back to back, no triangle is left lit against its winding, whatever the
// normals it starts from, and every normal stays of unit length.
#include "tile_normals.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

using terrain::Mesh;
using terrain::Vec3f;

float dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f unit(const Vec3f& v) {
    const float length = std::sqrt(dot(v, v));
    return {v.x / length, v.y / length, v.z / length};
}

Vec3f windingOf(const Mesh& m, std::size_t first) {
    const Vec3f& a = m.vertices[m.indices[first]];
    const Vec3f& b = m.vertices[m.indices[first + 1]];
    const Vec3f& c = m.vertices[m.indices[first + 2]];
    const Vec3f u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3f v{c.x - a.x, c.y - a.y, c.z - a.z};
    return {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
}

// A square of two triangles facing up (+y), its normals given.
Mesh square(const Vec3f& n0, const Vec3f& n1, const Vec3f& n2, const Vec3f& n3) {
    Mesh m;
    m.vertices = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
    m.normals = {n0, n1, n2, n3};
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}

void testFlat() {
    const Vec3f up{0.0f, 1.0f, 0.0f};
    const Vec3f leaning = unit({0.5f, 1.0f, 0.2f});
    Mesh m = square(up, leaning, up, {1.0f, 0.0f, 0.0f});
    m.normals[2] = {0.0f, -1.0f, 0.0f};
    terrain::tile::agreeWithTriangles(m);
    TEST_CHECK(m.normals[0] == up && m.normals[1] == leaning); // they agree: the same bits
    // Across the triangles: turned to a cosine of 0.3 with them, in the plane of x and y.
    TEST_CHECK(std::abs(dot(m.normals[3], up) - 0.3f) < 1e-5f && m.normals[3].x > 0.9f &&
               m.normals[3].z == 0.0f);
    TEST_CHECK(m.normals[2] == up); // against them: theirs
}

// The state of a generator of draws in [-1, 1).
float draw(std::uint64_t& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<float>(state >> 40U) * 0x1.0p-23f - 1.0f;
}

// A blade of rock along x, kBlade cells long, 2 * half thick at its foot and 1 high: its front
// faces +z, its back -z, both reaching the vertices of its crest.
Mesh blade(float half) {
    constexpr std::uint32_t kBlade = 16;
    Mesh m;
    for (std::uint32_t i = 0; i <= kBlade; ++i) {
        const auto x = static_cast<float>(i);
        m.vertices.push_back({x, 0.0f, half});  // 3 i: front foot
        m.vertices.push_back({x, 0.0f, -half}); // 3 i + 1: back foot
        m.vertices.push_back({x, 1.0f, 0.0f});  // 3 i + 2: crest
    }
    for (std::uint32_t i = 0; i < kBlade; ++i) {
        const std::uint32_t f = 3 * i;
        const std::uint32_t n = 3 * (i + 1);
        m.indices.insert(m.indices.end(), {f, n, n + 2, f, n + 2, f + 2});
        m.indices.insert(m.indices.end(), {n + 1, f + 1, f + 2, n + 1, f + 2, n + 2});
    }
    return m;
}

void testThinBlade() {
    std::size_t before = 0;
    std::size_t after = 0;
    bool units = true;
    std::uint64_t state = 0x7417ULL;
    for (int trial = 0; trial < 200; ++trial) {
        Mesh m = blade(0.02f + 0.1f * (0.5f + 0.5f * draw(state)));
        for (std::size_t v = 0; v < m.vertices.size(); ++v) {
            m.normals.push_back(unit({draw(state), draw(state), draw(state)}));
        }
        const auto folded = [](const Mesh& mesh) {
            std::size_t count = 0;
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                const Vec3f w = windingOf(mesh, t);
                float toward = 0.0f;
                for (std::size_t k = 0; k < 3; ++k) {
                    toward += dot(mesh.normals[mesh.indices[t + k]], w);
                }
                count += toward < 0.0f ? 1 : 0;
            }
            return count;
        };
        before += folded(m);
        terrain::tile::agreeWithTriangles(m);
        after += folded(m);
        for (const Vec3f& n : m.normals) {
            units = units && std::abs(dot(n, n) - 1.0f) < 1e-4f;
        }
    }
    std::printf("thin blades: %zu triangles lit against their winding before, %zu after\n",
                before, after);
    TEST_CHECK(before > 1'000);
    TEST_CHECK(after == 0);
    TEST_CHECK(units);
}

} // namespace

int main() {
    testFlat();
    testThinBlade();
    return test::result();
}
