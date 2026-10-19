#pragma once

#include <cstdint>
#include <vector>

namespace terrain {

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend bool operator==(const Vec3f&, const Vec3f&) = default;
};

// Indexed triangle mesh, positions in meters relative to the chunk origin.
// Triangles are counter-clockwise seen from the air; normals are unit length and point
// toward the air.
struct Mesh {
    std::vector<Vec3f> vertices;
    std::vector<Vec3f> normals;         // one per vertex
    std::vector<std::uint32_t> indices; // three per triangle

    friend bool operator==(const Mesh&, const Mesh&) = default;
};

} // namespace terrain
