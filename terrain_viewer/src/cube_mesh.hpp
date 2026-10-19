#pragma once

#include <glad/gl.h>

namespace viewer {

// Unit cube centered at the origin, one flat color per face.
// Vertex layout: location 0 = position (vec3), location 1 = color (vec3).
class CubeMesh {
public:
    CubeMesh();
    ~CubeMesh();

    CubeMesh(const CubeMesh&) = delete;
    CubeMesh& operator=(const CubeMesh&) = delete;

    void draw() const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};

} // namespace viewer
