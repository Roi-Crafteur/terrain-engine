// Maps of the relief: the grids hold the heights of the Heightfield at their sample centers bit
// for bit, whatever the number of threads; averaging, exaggeration and colors.
#include "terrain/io/relief_png.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

using terrain::ReliefGrid;

const terrain::WorldSize kWorld{16.0 * 65536.0, 8.0 * 65536.0};

bool sameBits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

void testWorldGrid() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, {}, kWorld);
    const ReliefGrid one = terrain::sampleReliefWorld(field, 16384.0, 1);
    const ReliefGrid three = terrain::sampleReliefWorld(field, 16384.0, 3);
    TEST_CHECK(one.columns == 64 && one.rows == 32 && one.wraps);
    TEST_CHECK(one.heights.size() == 64u * 32u);
    bool same = one.heights.size() == three.heights.size();
    for (int j = 0; j < one.rows && same; ++j) {
        for (int i = 0; i < one.columns; ++i) {
            same = same && sameBits(one.at(i, j), three.at(i, j)) &&
                   sameBits(one.at(i, j), field.heightAt(one.x(i), one.z(j)));
        }
    }
    TEST_CHECK(same);
    TEST_CHECK(one.x(0) == -0.5 * kWorld.x + 8192.0 && one.z(31) == 0.5 * kWorld.z - 8192.0);
    bool thrown = false;
    try {
        (void)terrain::sampleReliefWorld(field, 10000.0, 1);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    TEST_CHECK(thrown);
}

void testWindow() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, {}, kWorld);
    const ReliefGrid w = terrain::sampleReliefWindow(field, 1000.0, -2000.0, 10.0, 7, 5, 2);
    TEST_CHECK(w.columns == 7 && w.rows == 5 && !w.wraps);
    TEST_CHECK(w.x(0) == 1000.0 - 35.0 + 5.0 && w.z(4) == -2000.0 + 25.0 - 5.0);
    TEST_CHECK(sameBits(w.at(3, 2), field.heightAt(1000.0, -2000.0)));
    bool thrown = false;
    try {
        (void)terrain::sampleReliefWindow(field, 0.0, 0.0, 0.0, 4, 4, 1);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    TEST_CHECK(thrown);
}

void testDownsampleAndColors() {
    ReliefGrid g;
    g.columns = 4;
    g.rows = 2;
    g.spacing = 100.0;
    g.wraps = true;
    g.heights = {-4000.0f, -4000.0f, 1.0f, 3.0f, -4000.0f, -4000.0f, 5.0f, 7.0f};
    const ReliefGrid half = terrain::downsampleRelief(g, 2);
    TEST_CHECK(half.columns == 2 && half.rows == 1 && half.spacing == 200.0 && half.wraps);
    TEST_CHECK(half.heights[0] == -4000.0f && half.heights[1] == 4.0f);
    TEST_CHECK(!terrain::downsampleRelief(g, 3).wraps);
    TEST_CHECK(terrain::reliefExaggeration(10.0) == 1.0);
    TEST_CHECK(terrain::reliefExaggeration(1000.0) == 10.0);
    TEST_CHECK(terrain::reliefExaggeration(1.0e6) == 30.0);
    const terrain::Image image = terrain::renderRelief(half);
    TEST_CHECK(image.width == 2 && image.height == 1 && image.channels == 3);
    TEST_CHECK(image.pixels.size() == 6u);
    // Deep sea in blue, lowland in green.
    TEST_CHECK(image.pixels[2] > image.pixels[0] && image.pixels[2] > image.pixels[1]);
    TEST_CHECK(image.pixels[4] > image.pixels[3] && image.pixels[4] > image.pixels[5]);
}

} // namespace

int main() {
    testWorldGrid();
    testWindow();
    testDownsampleAndColors();
    return test::result();
}
