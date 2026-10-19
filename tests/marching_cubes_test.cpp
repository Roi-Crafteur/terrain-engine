#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/meshing/mesh.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <numbers>
#include <set>
#include <tuple>
#include <utility>

namespace {

using terrain::DensityGrid;
using terrain::kGridSamples;
using terrain::Mesh;
using terrain::Vec3f;

// ---------------------------------------------------------------------------
// Density fields (signed distance convention: negative is solid).

struct Sphere {
    double cx;
    double cy;
    double cz;
    double radius;

    double operator()(double x, double y, double z) const {
        const double dx = x - cx;
        const double dy = y - cy;
        const double dz = z - cz;
        return std::sqrt(dx * dx + dy * dy + dz * dz) - radius;
    }
};

// Surface y = slope * x + height.
struct Plane {
    double slope;
    double height;

    double operator()(double x, double y, double /*z*/) const { return y - slope * x - height; }
};

struct Heightmap {
    double operator()(double x, double y, double z) const {
        return y - (8.0 + 2.0 * std::sin(0.4 * x) * std::cos(0.3 * z));
    }
};

constexpr Sphere kOffGridSphere{8.25, 7.75, 8.5, 5.0};

// Deterministic density in [-1, -0.05] or [0.05, 1], never close to zero.
float hashedDensity(int x, int y, int z, std::uint32_t seed) {
    std::uint32_t h = seed * 0x9E3779B9U;
    for (const int v : {x, y, z}) {
        h ^= static_cast<std::uint32_t>(v) + 0x7F4A7C15U + (h << 6) + (h >> 2);
        h *= 0x85EBCA6BU;
        h ^= h >> 13;
    }
    const float magnitude = 0.05f + 0.95f * static_cast<float>(h & 0xFFFFU) / 65535.0f;
    return (h & 0x10000U) != 0 ? magnitude : -magnitude;
}

// Random interior samples; the outer layer is air so the surface is closed.
DensityGrid randomGrid(std::uint32_t seed) {
    DensityGrid grid(1.0f);
    for (int z = 1; z < kGridSamples - 1; ++z) {
        for (int y = 1; y < kGridSamples - 1; ++y) {
            for (int x = 1; x < kGridSamples - 1; ++x) {
                grid.set(x, y, z, hashedDensity(x, y, z, seed));
            }
        }
    }
    return grid;
}

// ---------------------------------------------------------------------------
// Geometry helpers.

Vec3f operator-(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3f operator+(Vec3f a, Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
double dot(Vec3f a, Vec3f b) {
    return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y +
           static_cast<double>(a.z) * b.z;
}
Vec3f cross(Vec3f a, Vec3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(Vec3f v) { return std::sqrt(dot(v, v)); }

std::size_t triangleCount(const Mesh& mesh) { return mesh.indices.size() / 3; }

// Corners of triangle i.
std::tuple<Vec3f, Vec3f, Vec3f> corners(const Mesh& mesh, std::size_t i) {
    return {mesh.vertices[mesh.indices[3 * i]], mesh.vertices[mesh.indices[3 * i + 1]],
            mesh.vertices[mesh.indices[3 * i + 2]]};
}

bool isEmpty(const Mesh& mesh) {
    return mesh.vertices.empty() && mesh.normals.empty() && mesh.indices.empty();
}

bool hasValidLayout(const Mesh& mesh) {
    if (mesh.normals.size() != mesh.vertices.size() || mesh.indices.size() % 3 != 0) {
        return false;
    }
    for (const std::uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            return false;
        }
    }
    return true;
}

// Every directed edge is used once and its reverse once: closed, consistently wound surface.
bool isClosedAndOriented(const Mesh& mesh) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        for (std::size_t k = 0; k < 3; ++k) {
            ++edges[{mesh.indices[i + k], mesh.indices[i + (k + 1) % 3]}];
        }
    }
    for (const auto& [edge, count] : edges) {
        const auto reverse = edges.find({edge.second, edge.first});
        if (count != 1 || reverse == edges.end() || reverse->second != 1) {
            return false;
        }
    }
    return !edges.empty();
}

// Divergence theorem; positive when triangles are counter-clockwise seen from outside.
double signedVolume(const Mesh& mesh) {
    double volume = 0.0;
    for (std::size_t i = 0; i < triangleCount(mesh); ++i) {
        const auto [a, b, c] = corners(mesh, i);
        volume += dot(a, cross(b, c)) / 6.0;
    }
    return volume;
}

double surfaceArea(const Mesh& mesh) {
    double area = 0.0;
    for (std::size_t i = 0; i < triangleCount(mesh); ++i) {
        const auto [a, b, c] = corners(mesh, i);
        area += length(cross(b - a, c - a)) / 2.0;
    }
    return area;
}

bool normalsAreUnit(const Mesh& mesh) {
    for (const Vec3f& n : mesh.normals) {
        if (!(std::abs(length(n) - 1.0) < 1e-4)) {
            return false;
        }
    }
    return true;
}

// Winding (geometric normal) and vertex normals agree on every non-degenerate triangle.
bool trianglesFaceTheirNormals(const Mesh& mesh) {
    for (std::size_t i = 0; i < triangleCount(mesh); ++i) {
        const auto [a, b, c] = corners(mesh, i);
        const Vec3f geometric = cross(b - a, c - a);
        const Vec3f shading = mesh.normals[mesh.indices[3 * i]] +
                              mesh.normals[mesh.indices[3 * i + 1]] +
                              mesh.normals[mesh.indices[3 * i + 2]];
        if (length(geometric) > 1e-6 && !(dot(geometric, shading) > 0.0)) {
            return false;
        }
    }
    return true;
}

// Moving along the normal increases the density: normals point toward the air.
template <class Field>
bool normalsPointToAir(const Mesh& mesh, const Field& field) {
    constexpr double kStep = 0.01;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3f p = mesh.vertices[i];
        const Vec3f n = mesh.normals[i];
        const double outside = field(p.x + kStep * n.x, p.y + kStep * n.y, p.z + kStep * n.z);
        const double inside = field(p.x - kStep * n.x, p.y - kStep * n.y, p.z - kStep * n.z);
        if (!(outside > inside)) {
            return false;
        }
    }
    return true;
}

bool verticesAreUnique(const Mesh& mesh) {
    std::set<std::tuple<float, float, float>> positions;
    for (const Vec3f& v : mesh.vertices) {
        positions.insert({v.x, v.y, v.z});
    }
    return positions.size() == mesh.vertices.size();
}

// Grid edges joining a solid sample to an air sample, counted from the grid alone.
std::size_t crossedEdgeCount(const DensityGrid& grid) {
    const auto solid = [&](int x, int y, int z) { return grid.at(x, y, z) < terrain::kIsoLevel; };
    std::size_t count = 0;
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            for (int x = 0; x < kGridSamples; ++x) {
                const bool here = solid(x, y, z);
                count += (x + 1 < kGridSamples && here != solid(x + 1, y, z)) ? 1U : 0U;
                count += (y + 1 < kGridSamples && here != solid(x, y + 1, z)) ? 1U : 0U;
                count += (z + 1 < kGridSamples && here != solid(x, y, z + 1)) ? 1U : 0U;
            }
        }
    }
    return count;
}

// One shared vertex per crossed grid edge: indexed mesh without duplicates.
bool hasOneVertexPerCrossedEdge(const Mesh& mesh, const DensityGrid& grid) {
    return mesh.vertices.size() == crossedEdgeCount(grid) && verticesAreUnique(mesh);
}

// ---------------------------------------------------------------------------
// Tests.

void testSampleDensityPlacesSamplesOnTheGrid() {
    const auto field = [](double x, double y, double z) { return x + 100.0 * y + 10000.0 * z; };
    const DensityGrid grid = terrain::sampleDensity(field, 32.0, -16.0, 48.0);
    TEST_CHECK(grid.at(0, 0, 0) == static_cast<float>(field(32.0, -16.0, 48.0)));
    TEST_CHECK(grid.at(16, 0, 0) == static_cast<float>(field(48.0, -16.0, 48.0)));
    TEST_CHECK(grid.at(3, 5, 7) == static_cast<float>(field(35.0, -11.0, 55.0)));
    TEST_CHECK(grid.at(16, 16, 16) == static_cast<float>(field(48.0, 0.0, 64.0)));
}

void testFullySolidGridHasNoSurface() {
    TEST_CHECK(isEmpty(terrain::marchingCubes(DensityGrid(-1.0f))));
}

void testFullyEmptyGridHasNoSurface() {
    TEST_CHECK(isEmpty(terrain::marchingCubes(DensityGrid(1.0f))));
}

void testHorizontalPlane() {
    const Plane plane{0.0, 7.3};
    const DensityGrid grid = terrain::sampleDensity(plane, 0.0, 0.0, 0.0);
    const Mesh mesh = terrain::marchingCubes(grid);
    TEST_CHECK(hasValidLayout(mesh));
    TEST_CHECK(mesh.vertices.size() == 17U * 17U);
    TEST_CHECK(triangleCount(mesh) == 16U * 16U * 2U);
    TEST_CHECK(hasOneVertexPerCrossedEdge(mesh, grid));
    TEST_CHECK(std::abs(surfaceArea(mesh) - 256.0) < 1e-3);
    bool onPlane = true;
    bool facingUp = true;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        onPlane = onPlane && std::abs(mesh.vertices[i].y - 7.3f) < 1e-5f;
        facingUp = facingUp && std::abs(mesh.normals[i].y - 1.0f) < 1e-6f;
    }
    TEST_CHECK(onPlane);
    TEST_CHECK(facingUp);
    TEST_CHECK(trianglesFaceTheirNormals(mesh));
}

void testTiltedPlane() {
    const Plane plane{0.3, 5.17};
    const DensityGrid grid = terrain::sampleDensity(plane, 0.0, 0.0, 0.0);
    const Mesh mesh = terrain::marchingCubes(grid);
    TEST_CHECK(hasValidLayout(mesh));
    TEST_CHECK(!mesh.indices.empty());
    TEST_CHECK(hasOneVertexPerCrossedEdge(mesh, grid));
    const double norm = std::sqrt(0.3 * 0.3 + 1.0);
    bool onPlane = true;
    bool alongGradient = true;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3f p = mesh.vertices[i];
        const Vec3f n = mesh.normals[i];
        onPlane = onPlane && std::abs(plane(p.x, p.y, p.z)) < 1e-4;
        alongGradient = alongGradient && std::abs(n.x + 0.3 / norm) < 1e-4 &&
                        std::abs(n.y - 1.0 / norm) < 1e-4 && std::abs(n.z) < 1e-6;
    }
    TEST_CHECK(onPlane);
    TEST_CHECK(alongGradient);
}

void testSphere() {
    const DensityGrid grid = terrain::sampleDensity(kOffGridSphere, 0.0, 0.0, 0.0);
    const Mesh mesh = terrain::marchingCubes(grid);
    TEST_CHECK(hasValidLayout(mesh));
    TEST_CHECK(isClosedAndOriented(mesh));
    TEST_CHECK(hasOneVertexPerCrossedEdge(mesh, grid));

    const Vec3f center{8.25f, 7.75f, 8.5f};
    bool onSphere = true;
    bool radialNormals = true;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3f radial = mesh.vertices[i] - center;
        const double distance = length(radial);
        onSphere = onSphere && std::abs(distance - kOffGridSphere.radius) < 0.06;
        radialNormals = radialNormals && dot(mesh.normals[i], radial) / distance > 0.99;
    }
    TEST_CHECK(onSphere);
    TEST_CHECK(radialNormals);

    const double r = kOffGridSphere.radius;
    const double expected = 4.0 / 3.0 * std::numbers::pi * r * r * r;
    const double volume = signedVolume(mesh);
    TEST_CHECK(volume > 0.0);
    TEST_CHECK(std::abs(volume - expected) / expected < 0.03);
}

// Samples exactly on the surface create coincident vertices but no hole and no NaN.
void testDensityExactlyZeroOnSamples() {
    const Sphere sphere{8.0, 8.0, 8.0, 5.0}; // (13, 8, 8) and (11, 12, 8) are at distance 5
    const DensityGrid grid = terrain::sampleDensity(sphere, 0.0, 0.0, 0.0);
    TEST_CHECK(grid.at(13, 8, 8) == 0.0f);
    const Mesh mesh = terrain::marchingCubes(grid);
    TEST_CHECK(hasValidLayout(mesh));
    TEST_CHECK(isClosedAndOriented(mesh));
    TEST_CHECK(mesh.vertices.size() == crossedEdgeCount(grid));
    bool finite = true;
    for (const Vec3f& v : mesh.vertices) {
        finite = finite && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    TEST_CHECK(finite);
    TEST_CHECK(normalsAreUnit(mesh));
    TEST_CHECK(signedVolume(mesh) > 0.0);
}

// Each of the 256 corner configurations of a cell, surrounded by air, must yield a closed,
// outward-facing surface: validates every row of the triangle table and the handling of
// ambiguous faces shared with neighbor cells.
void testAllCellConfigurations() {
    constexpr int kCell = 7;
    int failures = 0;
    for (unsigned cube = 0; cube < 256; ++cube) {
        DensityGrid grid(1.0f);
        for (unsigned corner = 0; corner < 8; ++corner) {
            if ((cube & (1U << corner)) == 0) {
                continue;
            }
            // Corner numbering of Paul Bourke's tables: 0-3 bottom face, i + 4 above i.
            const int x = kCell + ((corner & 3U) == 1 || (corner & 3U) == 2 ? 1 : 0);
            const int y = kCell + (corner >= 4 ? 1 : 0);
            const int z = kCell + ((corner & 3U) >= 2 ? 1 : 0);
            grid.set(x, y, z, -1.0f);
        }
        const Mesh mesh = terrain::marchingCubes(grid);
        const bool ok = cube == 0 ? isEmpty(mesh)
                                  : hasValidLayout(mesh) && isClosedAndOriented(mesh) &&
                                        hasOneVertexPerCrossedEdge(mesh, grid) &&
                                        signedVolume(mesh) > 0.0 && normalsAreUnit(mesh);
        if (!ok) {
            std::fprintf(stderr, "cell configuration %u: open, misoriented or invalid mesh\n",
                         cube);
            ++failures;
        }
    }
    TEST_CHECK(failures == 0);
}

void testRandomFieldIsClosed() {
    for (std::uint32_t seed = 1; seed <= 8; ++seed) {
        const DensityGrid grid = randomGrid(seed);
        const Mesh mesh = terrain::marchingCubes(grid);
        TEST_CHECK(hasValidLayout(mesh));
        TEST_CHECK(isClosedAndOriented(mesh));
        TEST_CHECK(hasOneVertexPerCrossedEdge(mesh, grid));
        TEST_CHECK(normalsAreUnit(mesh));
        TEST_CHECK(signedVolume(mesh) > 0.0);
    }
}

template <class Field>
void checkNormalConsistency(const Field& field) {
    const Mesh mesh = terrain::marchingCubes(terrain::sampleDensity(field, 0.0, 0.0, 0.0));
    TEST_CHECK(!mesh.indices.empty());
    TEST_CHECK(hasValidLayout(mesh));
    TEST_CHECK(normalsAreUnit(mesh));
    TEST_CHECK(normalsPointToAir(mesh, field));
    TEST_CHECK(trianglesFaceTheirNormals(mesh));
}

void testNormalConsistency() {
    checkNormalConsistency(Plane{0.0, 7.3});
    checkNormalConsistency(Plane{0.3, 5.17});
    checkNormalConsistency(Plane{-1.7, 20.1});
    checkNormalConsistency(kOffGridSphere);
    checkNormalConsistency(Sphere{8.0, 8.0, 8.0, 5.0});
    checkNormalConsistency(Sphere{8.0, 8.0, 8.0, 11.0}); // cut open by the chunk faces
    checkNormalConsistency(Heightmap{});

    const Mesh heightmap = terrain::marchingCubes(terrain::sampleDensity(Heightmap{}, 0, 0, 0));
    bool facingUp = true;
    for (const Vec3f& n : heightmap.normals) {
        facingUp = facingUp && n.y > 0.0f;
    }
    TEST_CHECK(facingUp);
}

void testDeterminism() {
    const DensityGrid first = terrain::sampleDensity(Heightmap{}, 0.0, 0.0, 0.0);
    const DensityGrid second = terrain::sampleDensity(Heightmap{}, 0.0, 0.0, 0.0);
    const Mesh mesh = terrain::marchingCubes(first);
    TEST_CHECK(!mesh.indices.empty());
    TEST_CHECK(terrain::marchingCubes(first) == mesh);
    TEST_CHECK(terrain::marchingCubes(second) == mesh);

    const DensityGrid noise = randomGrid(42);
    TEST_CHECK(terrain::marchingCubes(noise) == terrain::marchingCubes(randomGrid(42)));
    TEST_CHECK(!(terrain::marchingCubes(noise) == terrain::marchingCubes(randomGrid(43))));
}

} // namespace

int main() {
    testSampleDensityPlacesSamplesOnTheGrid();
    testFullySolidGridHasNoSurface();
    testFullyEmptyGridHasNoSurface();
    testHorizontalPlane();
    testTiltedPlane();
    testSphere();
    testDensityExactlyZeroOnSamples();
    testAllCellConfigurations();
    testRandomFieldIsClosed();
    testNormalConsistency();
    testDeterminism();
    return test::result();
}
