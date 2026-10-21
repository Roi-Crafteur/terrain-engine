// Meshes of the far horizon of the Unreal plugin, on the levels of a real clipmap with small
// windows: the vertex of a sample (altitude, the ocean flat at the sea level but never a lake
// above it, normal, color), the topology of a body, the bodies filled from the toroidal buffers
// (every sample at its place, the cells outside the hole drawn by two triangles facing up, the
// skirt facing inward below the border, the bounds below the curved ground), the borders
// stitched onto the edges of the coarser level bit for bit, and the trims around the finer
// windows.
#include "horizon_mesh.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using terrain::HorizonCell;
using terrain::HorizonLevel;
using terrain::HorizonParams;
using terrain::unreal::BodyShape;
using terrain::unreal::BodyTopology;
using terrain::unreal::CellRect;
using terrain::unreal::HorizonLook;
using terrain::unreal::HorizonVertex;
using terrain::unreal::UnrealMesh;
using terrain::unreal::UnrealVector3f;

constexpr int kCells = 64;
constexpr int kLevel0Hole = 10;

HorizonParams smallParams() {
    HorizonParams params;
    params.cellsPerSide = kCells;
    params.levels = 4;
    return params;
}

// The levels of a clipmap settled around an observer in the mountains.
struct Clipmap {
    Clipmap(double observerX, double observerZ) : x(observerX), z(observerZ) {
        horizon_test::settle(clipmap, sampler, x, z);
    }

    double x;
    double z;
    horizon_test::World world;
    HorizonParams params = smallParams();
    terrain::HorizonSampler sampler{world.field, world.ecology, params};
    terrain::HorizonClipmap clipmap{params};
};

UnrealVector3f minus(const UnrealVector3f& a, const UnrealVector3f& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

UnrealVector3f cross(const UnrealVector3f& a, const UnrealVector3f& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// The face Unreal draws in front: on the side of (c - a) x (b - a).
UnrealVector3f frontOf(const UnrealMesh& mesh, std::size_t triangle) {
    const auto at = [&](std::size_t corner) {
        return mesh.positions[static_cast<std::size_t>(mesh.triangles[3 * triangle + corner])];
    };
    return cross(minus(at(2), at(0)), minus(at(1), at(0)));
}

bool unit(const UnrealVector3f& v) {
    return std::abs(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) - 1.0f) < 1e-5f;
}

void testVertex() {
    const HorizonLook look{1.5, true};
    HorizonCell land;
    land.height = 100.0f;
    land.gradX = 0.5f;
    land.gradZ = -0.25f;
    const HorizonVertex ground = terrain::unreal::horizonVertex(land, look);
    TEST_CHECK(ground.z == static_cast<float>((100.0 - 1.5) * 100.0));
    const double length = std::sqrt(0.25 + 0.0625 + 1.0);
    TEST_CHECK(std::abs(ground.normal.x - static_cast<float>(-0.5 / length)) < 1e-6f);
    TEST_CHECK(std::abs(ground.normal.y - static_cast<float>(0.25 / length)) < 1e-6f);
    TEST_CHECK(std::abs(ground.normal.z - static_cast<float>(1.0 / length)) < 1e-6f);
    TEST_CHECK(ground.color == terrain::unreal::altitudeColor(100.0));
    HorizonCell ocean;
    ocean.height = -50.0f;
    ocean.gradX = 0.3f;
    ocean.flags = terrain::kHorizonWater;
    const HorizonVertex sea = terrain::unreal::horizonVertex(ocean, look);
    TEST_CHECK(sea.z == -150.0f);
    TEST_CHECK((sea.normal == UnrealVector3f{0.0f, 0.0f, 1.0f}));
    TEST_CHECK(sea.color == terrain::unreal::kSeaColor);
    const HorizonVertex seabed = terrain::unreal::horizonVertex(ocean, {1.5, false});
    TEST_CHECK(seabed.z == -5150.0f);
    TEST_CHECK(seabed.color == terrain::unreal::altitudeColor(-50.0));
    // A lake in the mountains keeps its ground.
    HorizonCell lake;
    lake.height = 1200.0f;
    lake.material = terrain::SurfaceMaterial::Water;
    lake.flags = terrain::kHorizonWater;
    TEST_CHECK(terrain::unreal::horizonVertex(lake, look).z == static_cast<float>(1198.5 * 100.0));
    TEST_CHECK(terrain::unreal::horizonVertex(lake, look).color ==
               terrain::unreal::altitudeColor(1200.0));
}

void testTopology() {
    const CellRect hole = terrain::unreal::centeredSquare(kCells, 17);
    const BodyTopology ring = terrain::unreal::bodyTopology(kCells, hole, false);
    TEST_CHECK(ring.samples.size() == 65u * 65u - 33u * 33u);
    TEST_CHECK(ring.border.size() == 4u * kCells);
    TEST_CHECK(ring.skirtFirst == static_cast<std::int32_t>(ring.samples.size()));
    TEST_CHECK(ring.seaFirst == -1);
    TEST_CHECK(ring.vertexCount() == ring.samples.size() + ring.border.size() + 1);
    TEST_CHECK(ring.triangles.size() == 3u * (2u * (64u * 64u - 34u * 34u) + 2u * 4u * kCells));
    bool inRange = true;
    for (const std::int32_t index : ring.triangles) {
        inRange = inRange && index >= 0 && index < ring.boundsVertex; // the bounds vertex unused
    }
    TEST_CHECK(inRange);
    const BodyTopology full = terrain::unreal::bodyTopology(kCells, CellRect{}, true);
    TEST_CHECK(full.samples.size() == 65u * 65u);
    TEST_CHECK(full.seaFirst == static_cast<std::int32_t>(65 * 65 + 4 * kCells));
    TEST_CHECK(full.vertexCount() == 65u * 65u + 4u * kCells + 5u);
    for (const CellRect& bad : {CellRect{0, 5, 10, 10}, CellRect{5, 5, 64, 10}}) {
        bool refused = false;
        try {
            static_cast<void>(terrain::unreal::bodyTopology(kCells, bad, false));
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        TEST_CHECK(refused);
    }
    bool odd = false;
    try {
        static_cast<void>(terrain::unreal::bodyTopology(63, CellRect{}, false));
    } catch (const std::invalid_argument&) {
        odd = true;
    }
    TEST_CHECK(odd);
}

// Triangles per cell of the grid, from the centroids of the first triangles of a mesh.
std::map<std::pair<int, int>, int> cellsOf(const UnrealMesh& mesh, std::size_t triangles,
                                           double pitchCm) {
    std::map<std::pair<int, int>, int> cells;
    for (std::size_t t = 0; t < triangles; ++t) {
        double x = 0.0;
        double y = 0.0;
        for (std::size_t c = 0; c < 3; ++c) {
            const auto v = static_cast<std::size_t>(mesh.triangles[3 * t + c]);
            x += mesh.positions[v].x / 3.0;
            y += mesh.positions[v].y / 3.0;
        }
        ++cells[{static_cast<int>(std::floor(x / pitchCm)), static_cast<int>(std::floor(y / pitchCm))}];
    }
    return cells;
}

// Cells of the rectangle outside the hole, each drawn by two triangles, and nothing else.
bool drawsExactly(const std::map<std::pair<int, int>, int>& cells, const CellRect& rect,
                  const CellRect& hole) {
    std::size_t expected = 0;
    for (int k = rect.k0; k < rect.k1; ++k) {
        for (int i = rect.i0; i < rect.i1; ++i) {
            if (hole.contains(i, k)) {
                continue;
            }
            ++expected;
            const auto found = cells.find({i, k});
            if (found == cells.end() || found->second != 2) {
                return false;
            }
        }
    }
    return cells.size() == expected;
}

struct LevelMesh {
    BodyTopology topology;
    UnrealMesh mesh;
};

LevelMesh body(const Clipmap& map, int n, const HorizonLook& look) {
    const HorizonLevel& level = map.clipmap.level(n);
    LevelMesh body;
    body.topology = terrain::unreal::bodyTopology(
        kCells, terrain::unreal::bodyHole(map.params, n, kLevel0Hole), n == 0);
    const BodyShape shape{n + 1 < map.params.levels, terrain::unreal::skirtMeters(level.pitch()),
                          100.0, 50.0};
    terrain::unreal::fillBody(level, body.topology, shape, look, body.mesh);
    body.mesh.triangles = body.topology.triangles;
    return body;
}

void checkBody(const Clipmap& map, int n, const LevelMesh& body, const HorizonLook& look) {
    const HorizonLevel& level = map.clipmap.level(n);
    const BodyTopology& topology = body.topology;
    const UnrealMesh& mesh = body.mesh;
    const double pitchCm = level.pitch() * 100.0;
    bool samples = true;
    for (std::size_t v = 0; v < topology.samples.size(); ++v) {
        const int i = topology.samples[v] % (kCells + 1);
        const int k = topology.samples[v] / (kCells + 1);
        const HorizonVertex vertex = terrain::unreal::horizonVertex(level.atWindow(i, k), look);
        const bool edge = i == 0 || k == 0 || i == kCells || k == kCells;
        const bool stitched = edge && n + 1 < map.params.levels && (i % 2 != 0 || k % 2 != 0);
        samples = samples && mesh.positions[v].x == static_cast<float>(i * pitchCm) &&
                  mesh.positions[v].y == static_cast<float>(k * pitchCm) &&
                  (stitched || mesh.positions[v].z == vertex.z) &&
                  mesh.normals[v] == vertex.normal && mesh.colors[v] == vertex.color &&
                  unit(mesh.normals[v]);
    }
    TEST_CHECK(samples);
    const CellRect& hole = topology.hole;
    const int holeCells = hole.empty() ? 0 : (hole.i1 - hole.i0) * (hole.k1 - hole.k0);
    const auto gridTriangles = static_cast<std::size_t>(2 * (kCells * kCells - holeCells));
    TEST_CHECK(drawsExactly(cellsOf(mesh, gridTriangles, pitchCm), {0, 0, kCells, kCells},
                            topology.hole));
    const std::size_t skirtTriangles = 2 * topology.border.size();
    const double center = kCells / 2 * pitchCm;
    bool up = true;
    bool inward = true;
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const UnrealVector3f front = frontOf(mesh, t);
        const bool skirt = t >= gridTriangles && t < gridTriangles + skirtTriangles;
        if (!skirt) {
            up = up && front.z > 0.0f;
            continue;
        }
        const UnrealVector3f a = mesh.positions[static_cast<std::size_t>(mesh.triangles[3 * t])];
        inward = inward && front.x * (center - a.x) + front.y * (center - a.y) > 0.0 &&
                 front.z == 0.0f;
    }
    TEST_CHECK(up);
    TEST_CHECK(inward);
    const auto skirtCm = static_cast<float>(terrain::unreal::skirtMeters(level.pitch()) * 100.0);
    bool hanging = true;
    float lowest = mesh.positions.front().z;
    for (std::size_t j = 0; j < topology.border.size(); ++j) {
        const UnrealVector3f top = mesh.positions[static_cast<std::size_t>(topology.border[j])];
        const UnrealVector3f below =
            mesh.positions[static_cast<std::size_t>(topology.skirtFirst) + j];
        hanging = hanging && below.x == top.x && below.y == top.y && below.z == top.z - skirtCm;
        lowest = std::min(lowest, below.z);
    }
    TEST_CHECK(hanging);
    const UnrealVector3f bounds = mesh.positions[static_cast<std::size_t>(topology.boundsVertex)];
    TEST_CHECK(bounds.z <= lowest - 5000.0f);
    TEST_CHECK(bounds.x == static_cast<float>(center) && bounds.y == static_cast<float>(center));
}

void checkSea(const LevelMesh& body, double pitchCm) {
    const BodyTopology& topology = body.topology;
    TEST_CHECK(topology.seaFirst >= 0);
    const UnrealVector3f low = body.mesh.positions[static_cast<std::size_t>(topology.seaFirst)];
    const UnrealVector3f high =
        body.mesh.positions[static_cast<std::size_t>(topology.seaFirst) + 3];
    const auto center = static_cast<float>(kCells / 2 * pitchCm);
    TEST_CHECK(low.z == 0.0f && high.z == 0.0f);
    TEST_CHECK(low.x == center - 10'000.0f && high.y == center + 10'000.0f);
    TEST_CHECK(body.mesh.colors[static_cast<std::size_t>(topology.seaFirst)] ==
               terrain::unreal::kSeaColor);
}

// The border of a level on the edges of the coarser one: its even samples at the altitudes of
// the coarser samples, bit for bit, and its odd samples halfway between them.
void checkStitch(const Clipmap& map, int n, const LevelMesh& fine, const HorizonLook& look) {
    const HorizonLevel& level = map.clipmap.level(n);
    const HorizonLevel& coarse = map.clipmap.level(n + 1);
    const BodyTopology& topology = fine.topology;
    const std::size_t count = topology.border.size();
    bool even = true;
    bool odd = true;
    for (std::size_t j = 0; j < count; ++j) {
        const UnrealVector3f vertex =
            fine.mesh.positions[static_cast<std::size_t>(topology.border[j])];
        if (j % 2 != 0) {
            const auto z = [&](std::size_t at) {
                return fine.mesh.positions[static_cast<std::size_t>(topology.border[at % count])].z;
            };
            odd = odd && vertex.z == 0.5f * (z(j - 1) + z(j + 1));
            continue;
        }
        const double x = level.originX(map.x) + vertex.x / 100.0;
        const double z = level.originZ(map.z) + vertex.y / 100.0;
        const double u = (x - coarse.originX(map.x)) / coarse.pitch();
        const double v = (z - coarse.originZ(map.z)) / coarse.pitch();
        even = even && u == std::floor(u) && v == std::floor(v) &&
               vertex.z == terrain::unreal::horizonVertex(
                               coarse.atWindow(static_cast<int>(u), static_cast<int>(v)), look)
                               .z;
    }
    TEST_CHECK(even);
    TEST_CHECK(odd);
}

void checkTrim(const Clipmap& map, int n, const LevelMesh& body, const HorizonLook& look) {
    const HorizonLevel& level = map.clipmap.level(n);
    const CellRect square = terrain::unreal::bodyHole(map.params, n, kLevel0Hole);
    const CellRect finer = terrain::unreal::finerWindowCells(level, map.clipmap.level(n - 1));
    const UnrealMesh trim = terrain::unreal::levelTrim(level, square, finer, look);
    const double pitchCm = level.pitch() * 100.0;
    TEST_CHECK(drawsExactly(cellsOf(trim, trim.triangleCount(), pitchCm), square, finer));
    bool up = true;
    for (std::size_t t = 0; t < trim.triangleCount(); ++t) {
        up = up && frontOf(trim, t).z > 0.0f;
    }
    TEST_CHECK(up);
    // Its vertices are the samples, as in the body where both meet.
    std::map<std::pair<float, float>, float> bodyAltitudes;
    for (std::size_t v = 0; v < body.topology.samples.size(); ++v) {
        bodyAltitudes[{body.mesh.positions[v].x, body.mesh.positions[v].y}] =
            body.mesh.positions[v].z;
    }
    bool same = true;
    std::size_t shared = 0;
    for (std::size_t v = 0; v < trim.positions.size(); ++v) {
        const auto i = static_cast<int>(trim.positions[v].x / pitchCm);
        const auto k = static_cast<int>(trim.positions[v].y / pitchCm);
        same = same && trim.positions[v].z ==
                           terrain::unreal::horizonVertex(level.atWindow(i, k), look).z;
        if (i == square.i0 || i == square.i1 || k == square.k0 || k == square.k1) {
            const auto found = bodyAltitudes.find({trim.positions[v].x, trim.positions[v].y});
            same = same && found != bodyAltitudes.end() && found->second == trim.positions[v].z;
            ++shared;
        }
    }
    TEST_CHECK(same);
    TEST_CHECK(shared >= 2u * static_cast<std::size_t>(square.i1 - square.i0));
    bool refused = false;
    try {
        static_cast<void>(terrain::unreal::levelTrim(level, finer, square, look));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
}

void testLevels(double x, double z) {
    const Clipmap map(x, z);
    const HorizonLook look{1.0, true};
    std::vector<LevelMesh> bodies;
    for (int n = 0; n < map.params.levels; ++n) {
        bodies.push_back(body(map, n, look));
        checkBody(map, n, bodies.back(), look);
        if (n > 0) {
            checkTrim(map, n, bodies.back(), look);
        }
    }
    checkSea(bodies.front(), map.clipmap.level(0).pitch() * 100.0);
    for (int n = 0; n + 1 < map.params.levels; ++n) {
        checkStitch(map, n, bodies[static_cast<std::size_t>(n)], look);
    }
    // Filled again: the same mesh.
    TEST_CHECK(body(map, 2, look).mesh == bodies[2].mesh);
}

} // namespace

int main() {
    testVertex();
    testTopology();
    testLevels(horizon_test::kX, horizon_test::kZ);
    testLevels(horizon_test::kX + 1'000.0 + 40.0, horizon_test::kZ - 530.0);
    // Across the corner of the world.
    const terrain::WorldSize world;
    testLevels(world.x / 2.0 - 100.0, world.z / 2.0 - 60.0);
    return test::result();
}
