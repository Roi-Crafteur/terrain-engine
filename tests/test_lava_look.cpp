// How the lava and the magma look in the viewer, computed on the CPU: one warm, bright color per
// lava vertex, apart from every material and from the lakes (water and ice), and the mix of the
// terrain shader between the lit color and the albedo (the lava glows, whatever the light).
#include "cave_look.hpp"

#include "terrain/generation/cave_materials.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace {

using terrain::CaveMaterial;
using viewer::Vec3;

// Distance below which two colors are taken for the same.
constexpr float kApart = 0.1f;

bool same(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool near(Vec3 a, Vec3 b, float tolerance = 1e-5f) {
    return viewer::length(a - b) <= tolerance;
}

bool inRange(Vec3 c) {
    return std::min({c.x, c.y, c.z}) >= 0.0f && std::max({c.x, c.y, c.z}) <= 1.0f;
}

float brightness(Vec3 c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

// A tile whose lava has a vertex per given normal (no triangles: only the vertices count).
terrain::TileData lavaTile(const std::vector<terrain::Vec3f>& normals) {
    terrain::TileData data;
    for (std::size_t i = 0; i < normals.size(); ++i) {
        data.lava.vertices.push_back({static_cast<float>(i), 0.0f, 0.0f});
    }
    data.lava.normals = normals;
    return data;
}

// Up, tilted, sideways and downward surfaces.
const std::vector<terrain::Vec3f> kNormals = {
    {0.0f, 1.0f, 0.0f}, {0.6f, 0.8f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};

// One color per lava vertex, none without lava, and a tile without normals still gets its colors.
void testOneColorPerVertex() {
    TEST_CHECK(viewer::lavaColors(terrain::TileData{}).empty());
    TEST_CHECK(viewer::lavaColors(lavaTile(kNormals)).size() == kNormals.size());
    TEST_CHECK(viewer::lavaColors(lavaTile(std::vector<terrain::Vec3f>(100))).size() == 100);

    terrain::TileData bare = lavaTile(kNormals);
    bare.lava.normals.clear();
    const std::vector<Vec3> colors = viewer::lavaColors(bare);
    TEST_CHECK(colors.size() == kNormals.size());
    TEST_CHECK(same(colors[0], viewer::lavaColors(lavaTile(kNormals))[0])); // as facing up
}

// Within [0, 1] and warm: red >= green >= blue, bright, with a yellow orange tone (green well
// over blue) on the surfaces facing up, whatever the normals (not even unit ones).
void testWarmAndBright() {
    std::vector<terrain::Vec3f> normals = kNormals;
    normals.push_back({0.0f, 5.0f, 0.0f});
    normals.push_back({0.0f, -5.0f, 0.0f});
    normals.push_back({0.0f, 0.0f, 0.0f});
    bool ranged = true;
    bool warm = true;
    bool bright = true;
    for (const Vec3 c : viewer::lavaColors(lavaTile(normals))) {
        ranged = ranged && inRange(c);
        warm = warm && c.x >= c.y && c.y >= c.z;
        bright = bright && c.x >= 0.95f && brightness(c) >= 0.4f;
    }
    TEST_CHECK(ranged);
    TEST_CHECK(warm);
    TEST_CHECK(bright);
    const Vec3 top = viewer::lavaColors(lavaTile(kNormals))[0];
    TEST_CHECK(top.y > 0.5f && top.z < 0.3f);
}

// The surfaces facing up are the brightest, the ones facing down the least, in between the
// brightness follows the normal; the two ends are apart.
void testBrighterFacingUp() {
    const std::vector<Vec3> c = viewer::lavaColors(lavaTile(kNormals));
    const float up = brightness(c[0]);
    const float tilted = brightness(c[1]);
    const float side = brightness(c[2]);
    const float down = brightness(c[3]);
    TEST_CHECK(up > tilted && tilted > side);
    TEST_CHECK(side == down);
    TEST_CHECK(viewer::length(c[0] - c[3]) > 0.2f);
}

// No material has the color of the lava, nor the lakes (water and ice) the color of the lava.
void testApartFromMaterialsAndLakes() {
    const std::vector<Vec3> lava = viewer::lavaColors(lavaTile(kNormals));
    float closest = 10.0f;
    for (int m = 0; m < terrain::kCaveMaterialCount; ++m) {
        const Vec3 material = viewer::caveMaterialColor(static_cast<CaveMaterial>(m));
        for (const Vec3 c : lava) {
            closest = std::min(closest, viewer::length(c - material));
        }
    }
    TEST_CHECK(closest > 0.2f);

    terrain::TileData lakes = lavaTile(kNormals);
    lakes.water = lakes.lava;
    lakes.waterMaterials = {CaveMaterial::Water, CaveMaterial::Ice, CaveMaterial::Water,
                            CaveMaterial::Ice};
    float apart = 10.0f;
    for (const Vec3 lake : viewer::lakeColors(lakes)) {
        for (const Vec3 c : lava) {
            apart = std::min(apart, viewer::length(c - lake));
        }
    }
    TEST_CHECK(apart > 0.5f);
}

// The magma of the walls over the lava differs from every other material, is bright and red
// orange, and is not the lava either; it stays warm after the tint of the volcanic districts.
void testMagmaColor() {
    const Vec3 magma = viewer::caveMaterialColor(CaveMaterial::Magma);
    TEST_CHECK(inRange(magma));
    TEST_CHECK(magma.x >= 0.85f && magma.x > magma.y && magma.y > magma.z);
    float closest = 10.0f;
    for (int m = 1; m < terrain::kCaveMaterialCount; ++m) {
        const auto material = static_cast<CaveMaterial>(m);
        if (material != CaveMaterial::Magma) {
            closest = std::min(closest,
                               viewer::length(magma - viewer::caveMaterialColor(material)));
        }
    }
    TEST_CHECK(closest > kApart);
    for (const Vec3 c : viewer::lavaColors(lavaTile(kNormals))) {
        TEST_CHECK(viewer::length(magma - c) > 0.2f);
    }
    // The walls of a tile carry it as they carry any material.
    terrain::TileData wall;
    wall.mesh.vertices = {{0.0f, 0.0f, 0.0f}};
    wall.mesh.normals = {{0.0f, 1.0f, 0.0f}};
    wall.materials = {CaveMaterial::Magma};
    TEST_CHECK(same(viewer::rockColors(wall)[0], magma));
}

// The mix of the terrain shader: the lit color without glow, the albedo with full glow, linear
// in between.
void testGlowColor() {
    const Vec3 lit{0.1f, 0.2f, 0.3f};
    const Vec3 albedo{0.9f, 0.5f, 0.1f};
    TEST_CHECK(same(viewer::glowColor(lit, albedo, 0.0f), lit));
    TEST_CHECK(same(viewer::glowColor(lit, albedo, 1.0f), albedo));
    for (const float t : {0.1f, 0.25f, 0.5f, 0.75f, 0.9f}) {
        TEST_CHECK(near(viewer::glowColor(lit, albedo, t), viewer::lerp(lit, albedo, t)));
    }
    const Vec3 half = viewer::glowColor(lit, albedo, 0.5f);
    TEST_CHECK(near(half, {0.5f, 0.35f, 0.2f}));
}

// With the glow of the lava, the light does not matter: in the dark and in the full light the
// lava shows its color, while the same color without glow follows the light.
void testLavaGlowsInTheDark() {
    TEST_CHECK(viewer::kLavaEmissive == 1.0f);
    const Vec3 lava = viewer::lavaColors(lavaTile(kNormals))[0];
    const Vec3 dark = lava * 0.25f; // the ambient light alone
    const Vec3 bright = lava * 1.9f;
    TEST_CHECK(same(viewer::glowColor(dark, lava, viewer::kLavaEmissive), lava));
    TEST_CHECK(same(viewer::glowColor(bright, lava, viewer::kLavaEmissive), lava));
    TEST_CHECK(same(viewer::glowColor(dark, lava, 0.0f), dark));
    TEST_CHECK(brightness(viewer::glowColor(dark, lava, viewer::kLavaEmissive)) >
               brightness(viewer::glowColor(dark, lava, 0.0f)) * 3.0f);
}

} // namespace

int main() {
    testOneColorPerVertex();
    testWarmAndBright();
    testBrighterFacingUp();
    testApartFromMaterialsAndLakes();
    testMagmaColor();
    testGlowColor();
    testLavaGlowsInTheDark();
    return test::result();
}
