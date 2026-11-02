// Views of meshes (phase 12A): the nearer triangle hides the farther one, the background shows
// where no triangle is drawn (under an overhang, through an opening), a triangle crossing the
// plane of the eye is cut and still drawn, the image never depends on the number of threads; from
// above, the highest surface shows.
#include "terrain/io/mesh_view.hpp"

#include "test_assert.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using terrain::Image;
using terrain::Mesh;
using terrain::Vec3f;
using terrain::ViewMesh;
using terrain::ViewParams;

constexpr std::array<std::uint8_t, 3> kBackground = {10, 20, 30};
constexpr std::array<std::uint8_t, 3> kWater = {55, 105, 165};

// A quad of corners a, b, c, d (counter-clockwise seen from the side of normal n).
void addQuad(Mesh& m, Vec3f a, Vec3f b, Vec3f c, Vec3f d, Vec3f n) {
    const auto first = static_cast<std::uint32_t>(m.vertices.size());
    for (const Vec3f& v : {a, b, c, d}) {
        m.vertices.push_back(v);
        m.normals.push_back(n);
    }
    for (const std::uint32_t i : {0U, 1U, 2U, 0U, 2U, 3U}) {
        m.indices.push_back(first + i);
    }
}

// A wall facing -Z (toward an eye at the origin looking north), depth meters away, as wide and
// high as half.
Mesh wallAt(float depth, float half) {
    Mesh m;
    addQuad(m, {-half, -half, -depth}, {half, -half, -depth}, {half, half, -depth},
            {-half, half, -depth}, {0.0f, 0.0f, 1.0f});
    return m;
}

ViewParams viewParams() {
    ViewParams p;
    p.width = 160;
    p.height = 90;
    p.heading = 0.0; // north, -Z
    return p;
}

Image background(const ViewParams& p) {
    Image image{p.width, p.height, 3, {}};
    for (int i = 0; i < p.width * p.height; ++i) {
        image.pixels.insert(image.pixels.end(), kBackground.begin(), kBackground.end());
    }
    return image;
}

std::array<std::uint8_t, 3> pixel(const Image& image, int column, int row) {
    const auto at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                     static_cast<std::size_t>(column)) *
                    3;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2]};
}

void testNearerHides() {
    const ViewParams p = viewParams();
    const Mesh near = wallAt(50.0f, 5.0f);
    const Mesh far = wallAt(100.0f, 50.0f);
    // The far wall drawn last: the near one still shows in front of it.
    const std::array<ViewMesh, 2> meshes{ViewMesh{&near, 0.0, 0.0, 0.0, {}, true, {}},
                                         ViewMesh{&far, 0.0, 0.0, 0.0, {}, false, {}}};
    const Image image = terrain::renderMeshView(meshes, background(p), p, 1);
    const auto center = pixel(image, p.width / 2, p.height / 2);
    // Water hazed by 50 m of distance: still nearer the water than anything else.
    TEST_CHECK(center[2] > center[0] && center != kBackground);
    const auto side = pixel(image, p.width / 2 + 20, p.height / 2);
    TEST_CHECK(side != center && side != kBackground);
    TEST_CHECK(pixel(image, 0, 0) == kBackground || pixel(image, 0, 0) != center);
}

// A slab overhanging nothing: the background shows below and above it.
void testBackgroundAroundAnOverhang() {
    ViewParams p = viewParams();
    Mesh slab;
    addQuad(slab, {-20.0f, 3.0f, -40.0f}, {20.0f, 3.0f, -40.0f}, {20.0f, 3.0f, -20.0f},
            {-20.0f, 3.0f, -20.0f}, {0.0f, -1.0f, 0.0f});
    p.eyeHeight = 0.0;
    const std::array<ViewMesh, 1> meshes{ViewMesh{&slab, 0.0, 0.0, 0.0, {}, false, {}}};
    const Image image = terrain::renderMeshView(meshes, background(p), p, 1);
    TEST_CHECK(pixel(image, p.width / 2, p.height / 2 + 5) == kBackground);
    TEST_CHECK(pixel(image, p.width / 2, p.height / 2 - 12) != kBackground);
    TEST_CHECK(pixel(image, p.width / 2, 0) == kBackground);
}

// A floor from behind the eye to far before it: cut at the plane of the eye, drawn below the
// horizon; and the same image on 1 and 4 threads.
void testClippedAndThreads() {
    ViewParams p = viewParams();
    p.eyeHeight = 2.0;
    Mesh floor;
    addQuad(floor, {-100.0f, 0.0f, 50.0f}, {100.0f, 0.0f, 50.0f}, {100.0f, 0.0f, -500.0f},
            {-100.0f, 0.0f, -500.0f}, {0.0f, 1.0f, 0.0f});
    const Mesh wall = wallAt(80.0f, 10.0f);
    const std::array<ViewMesh, 2> meshes{ViewMesh{&floor, 0.0, 0.0, 0.0, {}, false, {}},
                                         ViewMesh{&wall, 0.0, 0.0, 0.0, {}, false, {}}};
    const Image one = terrain::renderMeshView(meshes, background(p), p, 1);
    const Image four = terrain::renderMeshView(meshes, background(p), p, 4);
    TEST_CHECK(one.pixels == four.pixels);
    TEST_CHECK(pixel(one, p.width / 2, p.height - 1) != kBackground);
    TEST_CHECK(pixel(one, 2, p.height - 2) != kBackground);
    TEST_CHECK(pixel(one, p.width / 2, 0) == kBackground);
}

void testTopViewShowsTheHighest() {
    Mesh low;
    addQuad(low, {-10.0f, 0.0f, 10.0f}, {10.0f, 0.0f, 10.0f}, {10.0f, 0.0f, -10.0f},
            {-10.0f, 0.0f, -10.0f}, {0.0f, 1.0f, 0.0f});
    Mesh high;
    addQuad(high, {-2.0f, 5.0f, 2.0f}, {2.0f, 5.0f, 2.0f}, {2.0f, 5.0f, -2.0f},
            {-2.0f, 5.0f, -2.0f}, {0.0f, 1.0f, 0.0f});
    terrain::TopViewParams p;
    p.width = 40;
    p.height = 40;
    p.metersPerPixel = 1.0;
    // The low quad drawn last: the high one still shows.
    const std::array<ViewMesh, 2> meshes{ViewMesh{&high, 0.0, 0.0, 0.0, {}, true, {}},
                                         ViewMesh{&low, 0.0, 0.0, 0.0, {}, false, {}}};
    const Image one = terrain::renderTopView(meshes, p, 1);
    TEST_CHECK(pixel(one, 20, 20) == kWater);
    TEST_CHECK(pixel(one, 25, 20) != kWater);
    TEST_CHECK(pixel(one, 25, 20) != (std::array<std::uint8_t, 3>{0, 0, 0}));
    TEST_CHECK(pixel(one, 0, 0) == (std::array<std::uint8_t, 3>{0, 0, 0}));
    TEST_CHECK(terrain::renderTopView(meshes, p, 3).pixels == one.pixels);
}

void testInvalid() {
    const ViewParams p = viewParams();
    const Mesh wall = wallAt(10.0f, 1.0f);
    Mesh broken = wall;
    broken.indices.push_back(99);
    const std::array<ViewMesh, 1> bad{ViewMesh{&broken, 0.0, 0.0, 0.0, {}, false, {}}};
    const std::array<ViewMesh, 1> good{ViewMesh{&wall, 0.0, 0.0, 0.0, {}, false, {}}};
    const std::array<float, 1> shares{0.5f};
    const std::array<ViewMesh, 1> rock{ViewMesh{&wall, 0.0, 0.0, 0.0, shares, false, {}}};
    const auto throws = [](const auto& call) {
        try {
            call();
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    TEST_CHECK(throws([&] { (void)terrain::renderMeshView(bad, background(p), p, 1); }));
    TEST_CHECK(throws([&] { (void)terrain::renderMeshView(rock, background(p), p, 1); }));
    TEST_CHECK(throws([&] { (void)terrain::renderMeshView(good, Image{2, 2, 3, {}}, p, 1); }));
    terrain::TopViewParams top;
    top.metersPerPixel = 0.0;
    TEST_CHECK(throws([&] { (void)terrain::renderTopView(good, top, 1); }));
}

} // namespace

int main() {
    testNearerHides();
    testBackgroundAroundAnOverhang();
    testClippedAndThreads();
    testTopViewShowsTheHighest();
    testInvalid();
    return test::result();
}
