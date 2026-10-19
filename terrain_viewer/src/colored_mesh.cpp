#include "colored_mesh.hpp"

#include <cstddef>

namespace viewer {

static_assert(sizeof(ColoredVertex) == 6 * sizeof(float), "vertices are uploaded as is");

ColoredMesh::ColoredMesh(const std::vector<ColoredVertex>& vertices)
    : vertexCount_(static_cast<GLsizei>(vertices.size())) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(ColoredVertex)),
                 vertices.data(), GL_STATIC_DRAW);

    constexpr auto stride = static_cast<GLsizei>(sizeof(ColoredVertex));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(ColoredVertex, color)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

ColoredMesh::~ColoredMesh() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

void ColoredMesh::draw() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
}

} // namespace viewer
