#include "material_mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace viewer {

namespace {

// Position, normal, then the color with its opacity.
constexpr std::size_t kFloatsPerVertex = 10;
constexpr std::size_t kNormalOffset = 3;
constexpr std::size_t kColorOffset = 6;

std::vector<float> interleave(const terrain::Mesh& mesh, const std::vector<Vec3>& colors,
                              MeshOrigin origin, float opacity) {
    std::vector<float> data;
    data.reserve(mesh.vertices.size() * kFloatsPerVertex);
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const terrain::Vec3f p = mesh.vertices[i];
        const terrain::Vec3f n = mesh.normals[i];
        const Vec3 c = colors[i];
        data.insert(data.end(), {static_cast<float>(origin.x + p.x),
                                 static_cast<float>(origin.y + p.y),
                                 static_cast<float>(origin.z + p.z), n.x, n.y, n.z, c.x, c.y, c.z,
                                 opacity});
    }
    return data;
}

} // namespace

MaterialMesh::MaterialMesh(const terrain::Mesh& mesh, const std::vector<Vec3>& colors,
                           MeshOrigin origin, float opacity)
    : indexCount_(static_cast<GLsizei>(mesh.indices.size())) {
    if (colors.size() != mesh.vertices.size() || mesh.normals.size() != mesh.vertices.size()) {
        throw std::invalid_argument("MaterialMesh needs a normal and a color per vertex");
    }
    const std::vector<float> vertices = interleave(mesh, colors, origin, opacity);
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);

    constexpr auto stride = static_cast<GLsizei>(kFloatsPerVertex * sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(kNormalOffset * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(kColorOffset * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

MaterialMesh::~MaterialMesh() {
    glDeleteBuffers(1, &ebo_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

void MaterialMesh::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

} // namespace viewer
