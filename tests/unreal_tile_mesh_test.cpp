// Conversion of tile meshes for Unreal: vertex and triangle counts, centimeters and Z up, unit
// normals, the triangle order Unreal expects ((c - a) x (b - a) toward the air), the world
// position of the vertices, a tile of the wrapped world shown at another image, empty and
// malformed meshes, and the same arrays for the same tile.
#include "tile_mesh_conversion.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace {

using terrain::Mesh;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealMesh;
using terrain::unreal::UnrealPoint;
using terrain::unreal::UnrealVector3f;

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3d minus(const UnrealVector3f& a, const UnrealVector3f& b) {
    return {static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y,
            static_cast<double>(a.z) - b.z};
}

Vec3d minus(const terrain::Vec3f& a, const terrain::Vec3f& b) {
    return {static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y,
            static_cast<double>(a.z) - b.z};
}

Vec3d cross(const Vec3d& a, const Vec3d& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double length(const Vec3d& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Face normal of triangle i by the rule of Unreal, (c - a) x (b - a), in square centimeters.
Vec3d unrealFace(const UnrealMesh& mesh, std::size_t i) {
    const auto corner = [&](std::size_t k) {
        return mesh.positions[static_cast<std::size_t>(mesh.triangles[3 * i + k])];
    };
    return cross(minus(corner(2), corner(0)), minus(corner(1), corner(0)));
}

// Face normal of triangle i by the rule of the core, (b - a) x (c - a), in square meters.
Vec3d coreFace(const Mesh& mesh, std::size_t i) {
    const auto corner = [&](std::size_t k) { return mesh.vertices[mesh.indices[3 * i + k]]; };
    return cross(minus(corner(1), corner(0)), minus(corner(2), corner(0)));
}

// A counter-clockwise triangle of the core seen from the air, at altitude 10 m.
void testHandMadeTriangle() {
    Mesh mesh;
    mesh.vertices = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
    mesh.normals.assign(3, {0.0f, 1.0f, 0.0f});
    mesh.indices = {0, 1, 2};
    const UnrealMesh out = terrain::unreal::convertMesh(mesh, 10.0);
    TEST_CHECK(out.positions.size() == 3 && out.normals.size() == 3 && out.colors.size() == 3);
    TEST_CHECK(out.triangleCount() == 1);
    TEST_CHECK(out.positions[1] == (UnrealVector3f{0.0f, 100.0f, 0.0f}));
    TEST_CHECK(out.positions[2] == (UnrealVector3f{100.0f, 0.0f, 0.0f}));
    TEST_CHECK(out.normals[0] == (UnrealVector3f{0.0f, 0.0f, 1.0f}));
    TEST_CHECK((out.triangles == std::vector<std::int32_t>{0, 1, 2}));
    const Vec3d face = unrealFace(out, 0);
    TEST_CHECK(face.x == 0.0 && face.y == 0.0 && face.z == 10000.0); // toward the air
    TEST_CHECK(out.colors[0] == terrain::unreal::altitudeColor(10.0));
}

// A LOD 0 tile of a mountain: the arrays of the core, converted one to one.
void testMountainTile() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const terrain::ChunkCoord m = streaming_test::mountainChunk(tiles.heightfield());
    const TileData data = tiles.generate({{m, 0}, 0});
    const UnrealMesh out = terrain::unreal::convertMesh(data.mesh, data.originY);
    TEST_CHECK(!out.empty());
    TEST_CHECK(out.positions.size() == data.mesh.vertices.size());
    TEST_CHECK(out.normals.size() == data.mesh.normals.size());
    TEST_CHECK(out.triangles.size() == data.mesh.indices.size());
    bool sameIndices = true;
    for (std::size_t i = 0; i < out.triangles.size(); ++i) {
        sameIndices = sameIndices && out.triangles[i] == static_cast<int>(data.mesh.indices[i]);
    }
    TEST_CHECK(sameIndices);
    bool unitNormals = true;
    for (std::size_t i = 0; i < out.normals.size(); ++i) {
        const UnrealVector3f n = out.normals[i];
        unitNormals = unitNormals &&
                      n == terrain::unreal::toUnrealDirection(data.mesh.normals[i]) &&
                      std::abs(length(minus(n, {})) - 1.0) < 1e-5;
    }
    TEST_CHECK(unitNormals);
    // (c - a) x (b - a) in Unreal is the image of (b - a) x (c - a) in the core, times 100^2:
    // the front faces of Unreal look where the core triangles look, toward the air.
    bool sameFaces = true;
    std::size_t towardAir = 0;
    for (std::size_t i = 0; i < out.triangleCount(); ++i) {
        const Vec3d core = coreFace(data.mesh, i);
        const Vec3d image{core.x * 1e4, core.z * 1e4, core.y * 1e4};
        const Vec3d face = unrealFace(out, i);
        const double tolerance = 1e-2 + 1e-5 * length(image);
        sameFaces = sameFaces && length({face.x - image.x, face.y - image.y,
                                         face.z - image.z}) <= tolerance;
        towardAir += face.z > 0.0 ? 1 : 0;
    }
    TEST_CHECK(sameFaces);
    // A mountain slope, not an overhang: most of its faces look up (+Z).
    TEST_CHECK(towardAir * 10 > out.triangleCount() * 9);
}

// The world position of a vertex is the origin of its tile plus its local position.
void testWorldPositions() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const terrain::ChunkCoord m = streaming_test::mountainChunk(tiles.heightfield());
    const TileData data = tiles.generate({{m, 0}, 0});
    const UnrealFrame frame{{1000.25, -20.5, -333.0}};
    const UnrealPoint origin = terrain::unreal::tileOrigin(frame, data, data.request.key);
    TEST_CHECK(origin == terrain::unreal::toUnreal(frame, {data.originX, data.originY,
                                                          data.originZ}));
    const UnrealMesh out = terrain::unreal::convertMesh(data.mesh, data.originY);
    bool placed = true;
    for (std::size_t i = 0; i < out.positions.size(); ++i) {
        const streaming_test::WorldPoint w = streaming_test::worldVertex(data, i);
        const UnrealPoint expected = terrain::unreal::toUnreal(frame, {w.x, w.y, w.z});
        const UnrealVector3f local = out.positions[i];
        placed = placed && std::abs(origin.x + local.x - expected.x) < 1e-3 &&
                 std::abs(origin.y + local.y - expected.y) < 1e-3 &&
                 std::abs(origin.z + local.z - expected.z) < 1e-3;
    }
    TEST_CHECK(placed);
}

// The tile at the corner of the wrapped world, shown at its image one world west and one world
// north: its origin moves by whole worlds exactly, and its vertices lie on the ground there.
void testImages() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const terrain::WorldSize world = tiles.world();
    const terrain::ChunkCoord chunks = terrain::worldChunks(world);
    const terrain::TileKey corner{{chunks.x / 2 - 1, chunks.z / 2 - 1}, 0};
    const terrain::TileKey image{{corner.origin.x - chunks.x, corner.origin.z - chunks.z}, 0};
    const TileData data = tiles.generate({image, 0}); // generated at its canonical image
    TEST_CHECK(data.request.key == corner);
    TEST_CHECK(data == tiles.generate({corner, 0}));
    const UnrealFrame frame{{-30015000.0, 12.5, -17497000.0}};
    const UnrealPoint canonical = terrain::unreal::tileOrigin(frame, data, corner);
    const UnrealPoint shown = terrain::unreal::tileOrigin(frame, data, image);
    TEST_CHECK(shown.x == canonical.x - world.x * 100.0);
    TEST_CHECK(shown.y == canonical.y - world.z * 100.0);
    TEST_CHECK(shown.z == canonical.z);
    const UnrealMesh out = terrain::unreal::convertMesh(data.mesh, data.originY);
    bool onGround = !out.positions.empty();
    for (const terrain::unreal::UnrealVector3f& local : out.positions) {
        const terrain::unreal::CorePoint p = terrain::unreal::toCore(
            frame, {shown.x + local.x, shown.y + local.y, shown.z + local.z});
        // Linear interpolation along the edges of 1 m cells, as in terrain_tiles_test.
        onGround = onGround && std::abs(p.y - tiles.heightfield().heightAt(p.x, p.z)) <= 0.25;
    }
    TEST_CHECK(onGround);
}

bool throwsInvalid(const Mesh& mesh) {
    try {
        static_cast<void>(terrain::unreal::convertMesh(mesh, 0.0));
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void testEmptyAndMalformed() {
    const UnrealMesh empty = terrain::unreal::convertMesh({}, 0.0);
    TEST_CHECK(empty.empty() && empty.positions.empty() && empty.colors.empty());
    Mesh mesh;
    mesh.vertices = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
    mesh.normals.assign(3, {0.0f, 1.0f, 0.0f});
    mesh.indices = {0, 1, 2};
    TEST_CHECK(!throwsInvalid(mesh));
    Mesh missingNormal = mesh;
    missingNormal.normals.pop_back();
    TEST_CHECK(throwsInvalid(missingNormal));
    Mesh partialTriangle = mesh;
    partialTriangle.indices.push_back(0);
    TEST_CHECK(throwsInvalid(partialTriangle));
    Mesh outOfRange = mesh;
    outOfRange.indices[2] = 3;
    TEST_CHECK(throwsInvalid(outOfRange));
}

// The same tile, generated by two worlds of the same seed, gives the same arrays.
void testDeterministic() {
    const TerrainTiles a(streaming_test::kSeed);
    const TerrainTiles b(streaming_test::kSeed);
    const terrain::ChunkCoord m = streaming_test::mountainChunk(a.heightfield());
    const UnrealFrame frame{{-17.0, 3.0, 4096.0}};
    for (const unsigned seams : {0U, terrain::kSeamNegX | terrain::kSeamPosZ}) {
        const TileData first = a.generate({{m, 0}, seams});
        const TileData second = b.generate({{m, 0}, seams});
        const UnrealMesh converted = terrain::unreal::convertMesh(first.mesh, first.originY);
        TEST_CHECK(!converted.empty());
        TEST_CHECK(converted == terrain::unreal::convertMesh(second.mesh, second.originY));
        TEST_CHECK(terrain::unreal::tileOrigin(frame, first, first.request.key) ==
                   terrain::unreal::tileOrigin(frame, second, second.request.key));
    }
}

} // namespace

int main() {
    testHandMadeTriangle();
    testMountainTile();
    testWorldPositions();
    testImages();
    testEmptyAndMalformed();
    testDeterministic();
    return test::result();
}
