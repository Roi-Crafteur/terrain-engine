// How the caves look in the viewer, computed on the CPU: a distinct color per material, the
// altitude tint of the outer surface, the colors of the rock and lake vertices of a tile, and the
// speleothem cones (counts, tips, winding toward the outside, colors).
#include "altitude_tint.hpp"
#include "cave_look.hpp"

#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using terrain::CaveMaterial;
using viewer::Vec3;

bool same(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Every material of a cave wall or a lake has its own color, within [0, 1].
void testPalette() {
    bool distinct = true;
    bool inRange = true;
    for (int a = 1; a < terrain::kCaveMaterialCount; ++a) {
        const Vec3 color = viewer::caveMaterialColor(static_cast<CaveMaterial>(a));
        inRange = inRange && color.x >= 0.0f && color.x <= 1.0f && color.y >= 0.0f &&
                  color.y <= 1.0f && color.z >= 0.0f && color.z <= 1.0f;
        for (int b = 1; b < a; ++b) {
            const Vec3 other = viewer::caveMaterialColor(static_cast<CaveMaterial>(b));
            distinct = distinct && !same(color, other);
        }
    }
    TEST_CHECK(distinct);
    TEST_CHECK(inRange);
}

// The outer surface is green at sea level and gray from the high altitude, as in the streamed
// world, and the tint is clamped beyond them.
void testAltitudeTint() {
    const Vec3 low = viewer::altitudeColor(viewer::kStreamedLowAltitude);
    const Vec3 high = viewer::altitudeColor(viewer::kStreamedHighAltitude);
    TEST_CHECK(same(low, viewer::altitudeColor(-500.0f)));
    TEST_CHECK(same(high, viewer::altitudeColor(9000.0f)));
    TEST_CHECK(low.y > low.x && high.x > low.x);
    const Vec3 middle = viewer::altitudeColor(viewer::kStreamedHighAltitude / 2.0f);
    TEST_CHECK(middle.x > low.x && middle.x < high.x);
}

terrain::TileData tileWithMaterials() {
    terrain::TileData data;
    data.originY = 100.0;
    data.mesh.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, -50.0f, 0.0f}, {0.0f, -60.0f, 1.0f}};
    data.mesh.normals = {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    data.mesh.indices = {0, 1, 2};
    data.materials = {CaveMaterial::Surface, CaveMaterial::Limestone, CaveMaterial::Ice};
    data.water = data.mesh;
    data.waterMaterials = {CaveMaterial::Water, CaveMaterial::Ice, CaveMaterial::Water};
    return data;
}

// Rock vertices take the color of their material, the outer surface its altitude tint (and so
// does a tile without materials); lake vertices are water or ice.
void testTileColors() {
    terrain::TileData data = tileWithMaterials();
    const std::vector<Vec3> rock = viewer::rockColors(data);
    TEST_CHECK(rock.size() == 3);
    TEST_CHECK(same(rock[0], viewer::altitudeColor(100.0f)));
    TEST_CHECK(same(rock[1], viewer::caveMaterialColor(CaveMaterial::Limestone)));
    TEST_CHECK(same(rock[2], viewer::caveMaterialColor(CaveMaterial::Ice)));
    const std::vector<Vec3> lakes = viewer::lakeColors(data);
    TEST_CHECK(lakes.size() == 3);
    TEST_CHECK(same(lakes[0], viewer::caveMaterialColor(CaveMaterial::Water)));
    TEST_CHECK(same(lakes[1], viewer::caveMaterialColor(CaveMaterial::Ice)));

    data.materials.clear();
    const std::vector<Vec3> plain = viewer::rockColors(data);
    TEST_CHECK(same(plain[1], viewer::altitudeColor(50.0f)));
}

// Cones: the same number of vertices per speleothem, tips below the base of a stalactite and
// above the base of a stalagmite, triangles turning counter-clockwise seen from outside (their
// geometric normal points away from the axis), ice in its color.
void testSpeleothemCones() {
    const std::vector<terrain::Speleothem> speleothems = {
        {terrain::SpeleothemKind::Stalactite, 10.0, 20.0, -30.0f, 2.0f, 0.4f, false},
        {terrain::SpeleothemKind::Stalagmite, 10.0, 20.0, -36.0f, 1.0f, 0.3f, true},
        {terrain::SpeleothemKind::Stalagmite, 0.0, 0.0, 0.0f, 0.0f, 0.3f, false}, // no length
    };
    const viewer::SpeleothemMesh cones = viewer::speleothemMesh(speleothems);
    const std::size_t perCone = cones.mesh.vertices.size() / 2;
    TEST_CHECK(cones.mesh.vertices.size() == 2 * perCone && perCone >= 6);
    TEST_CHECK(cones.colors.size() == cones.mesh.vertices.size());
    TEST_CHECK(cones.mesh.normals.size() == cones.mesh.vertices.size());
    TEST_CHECK(cones.mesh.indices.size() % 3 == 0 && !cones.mesh.indices.empty());

    float lowest = 0.0f;
    float highest = -100.0f;
    for (std::size_t i = 0; i < perCone; ++i) {
        lowest = std::min(lowest, cones.mesh.vertices[i].y);
        highest = std::max(highest, cones.mesh.vertices[perCone + i].y);
    }
    TEST_CHECK(std::abs(lowest - (-32.0f)) < 1e-4f);  // stalactite tip: base - length
    TEST_CHECK(std::abs(highest - (-35.0f)) < 1e-4f); // stalagmite tip: base + length
    TEST_CHECK(same(cones.colors[perCone], viewer::caveMaterialColor(CaveMaterial::Ice)) ||
               same(cones.colors.back(), viewer::caveMaterialColor(CaveMaterial::Ice)));

    bool outward = true;
    for (std::size_t t = 0; t + 2 < cones.mesh.indices.size(); t += 3) {
        const auto at = [&](std::size_t k) {
            const terrain::Vec3f v = cones.mesh.vertices[cones.mesh.indices[t + k]];
            return Vec3{v.x, v.y, v.z};
        };
        const Vec3 a = at(0);
        const Vec3 normal = viewer::cross(at(1) - a, at(2) - a);
        const Vec3 center = (a + at(1) + at(2)) * (1.0f / 3.0f);
        const Vec3 away{center.x - 10.0f, 0.0f, center.z - 20.0f};
        outward = outward && viewer::dot(normal, away) > 0.0f;
    }
    TEST_CHECK(outward);
}

} // namespace

int main() {
    testPalette();
    testAltitudeTint();
    testTileColors();
    testSpeleothemCones();
    return test::result();
}
