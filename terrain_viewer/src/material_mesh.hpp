#pragma once

#include "math.hpp"
#include "terrain/meshing/mesh.hpp"

#include <glad/gl.h>

#include <vector>

namespace viewer {

// World position, in meters, of the origin of a mesh (the origin of a tile).
struct MeshOrigin {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// GPU copy of an indexed mesh with one color per vertex, placed in world meters.
// Vertex layout: location 0 = position (vec3), location 1 = normal (vec3),
// location 2 = color (vec4: red, green, blue, then the opacity of the mesh).
class MaterialMesh {
public:
    // The position of a vertex on the GPU is origin + its position in mesh. colors holds one
    // color per vertex of mesh (std::invalid_argument otherwise); opacity (1: opaque) is the
    // alpha of every vertex, written by the fragment shader.
    MaterialMesh(const terrain::Mesh& mesh, const std::vector<Vec3>& colors, MeshOrigin origin = {},
                 float opacity = 1.0f);
    ~MaterialMesh();

    MaterialMesh(const MaterialMesh&) = delete;
    MaterialMesh& operator=(const MaterialMesh&) = delete;

    void draw() const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;
};

} // namespace viewer
