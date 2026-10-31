// The layers of the dev map and their renderer (map_layers.hpp, map_style.hpp, map_render.hpp)
// on a synthetic world (map_test_support.hpp): nothing here depends on the generation.
#include "terrain/map/map_render.hpp"
#include "terrain/map/map_style.hpp"

#include "map_test_support.hpp"
#include "test_assert.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace terrain;

namespace {

MapColor solid(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return {r, g, b, a};
}

void testRegistry() {
    MapLayers layers;
    layers.add("a", [](const MapPoint&) { return solid(255, 0, 0, 255); });
    layers.add("b", [](const MapPoint&) { return solid(0, 255, 0, 255); });
    layers.add("c", [](const MapPoint&) { return solid(0, 0, 255, 255); });
    TEST_CHECK((layers.names() == std::vector<std::string>{"a", "b", "c"}));
    // Replaced in place, its order kept.
    layers.add("a", [](const MapPoint&) { return solid(9, 9, 9, 255); });
    TEST_CHECK((layers.names() == std::vector<std::string>{"a", "b", "c"}));
    TEST_CHECK(layers.remove("b") && !layers.remove("b"));
    TEST_CHECK(layers.find("b") == nullptr && layers.find("c") != nullptr);
    // Enabled in the order of the registry, not of the request.
    const auto enabled = layers.enabled({"c", "a"});
    TEST_CHECK(enabled.size() == 2 && enabled[0] == layers.find("a") &&
               enabled[1] == layers.find("c"));
    int refused = 0;
    try {
        (void)layers.enabled({"nope"});
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    try {
        layers.add("", [](const MapPoint&) { return MapColor{}; });
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    try {
        layers.add("d", MapLayerFn{});
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    TEST_CHECK(refused == 3);
    const MapLayers builtin = defaultMapLayers();
    TEST_CHECK((builtin.names() ==
                std::vector<std::string>{"relief", "biomes", "water", "contours"}));
}

void testOver() {
    const MapColor red = solid(200, 10, 10, 255);
    const MapColor blue = solid(10, 20, 200, 255);
    TEST_CHECK(over(red, blue) == blue);
    TEST_CHECK(over(red, MapColor{}) == red);
    TEST_CHECK(over(MapColor{}, blue) == blue);
    TEST_CHECK(over(MapColor{}, MapColor{}) == MapColor{});
    const MapColor half = over(solid(0, 0, 0, 255), solid(255, 255, 255, 128));
    TEST_CHECK(half.a == 255 && half.r == 128 && half.g == 128 && half.b == 128);
    // Two translucent colors: the alpha of the result is a + b (1 - a).
    const MapColor both = over(solid(0, 0, 255, 128), solid(255, 0, 0, 128));
    TEST_CHECK(both.a == 192 && both.r == 170 && both.b == 85);
}

// 4 m per pixel over the sea (west), the fall mark, the lake and the river (east).
const MapFrame kFrame({-600.0, -120.0, 1200.0, 480.0}, 300, 120);

// The default layers on the synthetic world: the same bytes on 1 and 4 threads, a fixed
// fingerprint (the style of the map, not the world), and the parts of the world in their colors.
void testDefaultImage() {
    const map_test::SyntheticSampler sampler;
    const MapLayers layers = defaultMapLayers();
    const std::vector<std::string> all = layers.names();
    const MapImage one = renderMap(sampler, kFrame, layers, all, {1});
    const MapImage four = renderMap(sampler, kFrame, layers, all, {4});
    TEST_CHECK(one.rgba == four.rgba);
    TEST_CHECK(one.width == 300 && one.height == 120 && one.rgba.size() == 300u * 120u * 4u);
    TEST_CHECK(one.stride == 1 && one.stats.samples == 302u * 122u);
    const std::uint64_t print = map_test::fnv1a(one.rgba);
    std::printf("synthetic image: 0x%016llXULL\n", static_cast<unsigned long long>(print));
    TEST_CHECK(print == 0x03FDC4C25BC6FBA8ULL);
    // The mark of the fall at the origin: its red ring (no pixel center falls in its center).
    const MapColor ring = one.at(static_cast<int>(kFrame.columnAt(2.0)),
                                 static_cast<int>(kFrame.rowAt(2.0)));
    TEST_CHECK(ring.r == 205 && ring.g == 30 && ring.b == 40);
    // The sea in the west: blue over its floor.
    const MapColor sea = one.at(2, 2);
    TEST_CHECK(sea.b > sea.g && sea.g > sea.r);
    // The lake, and the river along x = 300.
    const MapColor lake = one.at(static_cast<int>(kFrame.columnAt(150.0)),
                                static_cast<int>(kFrame.rowAt(150.0)));
    TEST_CHECK(lake.b > lake.r && lake.b > lake.g);
    // Its pixels cover half of it: blue laid at half over the sand of the land (the biomes).
    const MapColor river = one.at(static_cast<int>(kFrame.columnAt(298.0)), 100);
    const MapColor land = one.at(static_cast<int>(kFrame.columnAt(320.0)), 100);
    TEST_CHECK(river.b > river.r && river.r < land.r - 60 && river.b > land.b + 15);
}

// A layer another module registers (the kingdoms...) changes the image only where it lays a
// color, exactly as "over" the image without it: the renderer is not touched.
void testExternalLayer() {
    const map_test::SyntheticSampler sampler;
    MapLayers layers = defaultMapLayers();
    const std::vector<std::string> base = defaultMapLayerNames();
    const MapImage before = renderMap(sampler, kFrame, layers, base);
    const auto kingdom = [](const MapPoint& p) {
        return p.x > 100.0 ? solid(180, 0, 160, 96) : MapColor{};
    };
    layers.add("kingdoms", kingdom);
    std::vector<std::string> with = base;
    with.push_back("kingdoms");
    const MapImage after = renderMap(sampler, kFrame, layers, with);
    bool exact = true;
    for (int j = 0; j < kFrame.height(); ++j) {
        for (int i = 0; i < kFrame.width(); ++i) {
            MapPoint p;
            p.x = kFrame.pixelX(i);
            exact = exact && after.at(i, j) == over(before.at(i, j), kingdom(p));
        }
    }
    TEST_CHECK(exact);
    TEST_CHECK(after.rgba != before.rgba);
}

// The contours of a slope of 0.05 m per meter at 1 m per pixel: every 10 m, one column in 200.
void testContours() {
    const map_test::SyntheticSampler sampler;
    const MapLayers layers = defaultMapLayers();
    const MapFrame strip({1000.0, 1000.0, 800.0, 1.0}, 800, 1);
    TEST_CHECK(mapContourInterval(1.0) == 10.0);
    const MapImage image = renderMap(sampler, strip, layers, {"contours"});
    int lines = 0;
    for (int i = 0; i < 800; ++i) {
        lines += image.at(i, 0).a > 0 ? 1 : 0;
    }
    TEST_CHECK(lines == 4);
}

// Above the cap, the image is sampled every stride pixels, each sample on its block.
void testStride() {
    TEST_CHECK(mapStride(256, 256, kMapMaxSamples) == 1);
    TEST_CHECK(mapStride(4096, 4096, kMapMaxSamples) == 5);
    TEST_CHECK(mapStride(100, 100, 9) == 100);
    const map_test::SyntheticSampler sampler;
    const MapLayers layers = defaultMapLayers();
    const MapFrame big({-1000.0, -1000.0, 2000.0, 1500.0}, 2000, 1500);
    const MapImage capped = renderMap(sampler, big, layers, {"relief"}, {2, 40000});
    TEST_CHECK(capped.stride == mapStride(2000, 1500, 40000) && capped.stride > 1);
    TEST_CHECK(capped.stats.samples <= 40000u);
    TEST_CHECK(capped.width == 2000 && capped.height == 1500);
    const MapImage coarse = renderMap(sampler, strideFrame(big, capped.stride), layers, {"relief"});
    TEST_CHECK(capped.at(0, 0) == coarse.at(0, 0));
    TEST_CHECK(capped.at(1999, 1499) == coarse.at(coarse.width - 1, coarse.height - 1));
    bool refused = false;
    try {
        (void)mapStride(10, 10, 8);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
}

} // namespace

int main() {
    testRegistry();
    testOver();
    testDefaultImage();
    testExternalLayer();
    testContours();
    testStride();
    return test::result();
}
