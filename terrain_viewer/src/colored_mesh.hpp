#pragma once

#include "math.hpp"

#include <glad/gl.h>

#include <vector>

namespace viewer {

struct ColoredVertex {
    Vec3 position;
    Vec3 color;
};

// GPU copy of a triangle list with one color per vertex (three vertices per triangle).
// Vertex layout: location 0 = position (vec3), location 1 = color (vec3).
class ColoredMesh {
public:
    explicit ColoredMesh(const std::vector<ColoredVertex>& vertices);
    ~ColoredMesh();

    ColoredMesh(const ColoredMesh&) = delete;
    ColoredMesh& operator=(const ColoredMesh&) = delete;

    void draw() const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei vertexCount_ = 0;
};

} // namespace viewer
