#include "chunk_mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace viewer {

namespace {

constexpr std::size_t kFloatsPerVertex = 6;

std::vector<float> interleave(const terrain::Mesh& mesh) {
    std::vector<float> data;
    data.reserve(mesh.vertices.size() * kFloatsPerVertex);
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const terrain::Vec3f p = mesh.vertices[i];
        const terrain::Vec3f n = mesh.normals[i];
        data.insert(data.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
    }
    return data;
}

} // namespace

ChunkMesh::ChunkMesh(const terrain::Mesh& mesh)
    : indexCount_(static_cast<GLsizei>(mesh.indices.size())) {
    const std::vector<float> vertices = interleave(mesh);
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
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

ChunkMesh::~ChunkMesh() {
    glDeleteBuffers(1, &ebo_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

void ChunkMesh::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

} // namespace viewer
