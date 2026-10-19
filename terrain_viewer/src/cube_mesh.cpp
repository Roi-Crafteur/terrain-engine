#include "cube_mesh.hpp"

#include "math.hpp"

#include <array>
#include <cstddef>

namespace viewer {

namespace {

// Corners of the unit cube: bit 0 = +X, bit 1 = +Y, bit 2 = +Z.
constexpr Vec3 corner(int bits) {
    return {(bits & 1) != 0 ? 0.5f : -0.5f, (bits & 2) != 0 ? 0.5f : -0.5f,
            (bits & 4) != 0 ? 0.5f : -0.5f};
}

// Corners listed counter-clockwise when seen from outside the cube.
struct Face {
    std::array<int, 4> corners;
    Vec3 color;
};

constexpr std::array<Face, 6> kFaces{{
    {{5, 1, 3, 7}, {0.90f, 0.30f, 0.25f}}, // +X
    {{0, 4, 6, 2}, {0.30f, 0.75f, 0.35f}}, // -X
    {{6, 7, 3, 2}, {0.30f, 0.45f, 0.90f}}, // +Y
    {{0, 1, 5, 4}, {0.95f, 0.80f, 0.25f}}, // -Y
    {{4, 5, 7, 6}, {0.85f, 0.35f, 0.85f}}, // +Z
    {{1, 0, 2, 3}, {0.25f, 0.85f, 0.85f}}, // -Z
}};

constexpr std::array<std::size_t, 6> kQuadTriangles{0, 1, 2, 0, 2, 3};
constexpr std::size_t kFloatsPerVertex = 6;
constexpr std::size_t kVertexCount = kFaces.size() * kQuadTriangles.size();

using VertexData = std::array<float, kVertexCount * kFloatsPerVertex>;

constexpr VertexData buildVertices() {
    VertexData data{};
    std::size_t i = 0;
    for (const Face& face : kFaces) {
        for (const std::size_t index : kQuadTriangles) {
            const Vec3 p = corner(face.corners[index]);
            for (const float value : {p.x, p.y, p.z, face.color.x, face.color.y, face.color.z}) {
                data[i++] = value;
            }
        }
    }
    return data;
}

constexpr VertexData kVertices = buildVertices();

} // namespace

CubeMesh::CubeMesh() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices.data(), GL_STATIC_DRAW);

    constexpr auto stride = static_cast<GLsizei>(kFloatsPerVertex * sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

CubeMesh::~CubeMesh() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

void CubeMesh::draw() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(kVertexCount));
}

} // namespace viewer
