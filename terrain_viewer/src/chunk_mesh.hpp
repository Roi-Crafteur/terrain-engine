#pragma once

#include "terrain/meshing/mesh.hpp"

#include <glad/gl.h>

namespace viewer {

// GPU copy of an indexed terrain mesh.
// Vertex layout: location 0 = position (vec3), location 1 = normal (vec3).
class ChunkMesh {
public:
    explicit ChunkMesh(const terrain::Mesh& mesh);
    ~ChunkMesh();

    ChunkMesh(const ChunkMesh&) = delete;
    ChunkMesh& operator=(const ChunkMesh&) = delete;

    void draw() const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;
};

} // namespace viewer
