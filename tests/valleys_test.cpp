// The tributary valleys of the mountains of the geography (valleys.hpp): spacings that tile the
// world; shares within their bounds, 1 on flat regional elevation; deterministic, continuous,
// across the seams of the world too; valleys about one spacing apart across the slope.
#include "valleys.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using terrain::WorldSize;
namespace valleys = terrain::valleys;

constexpr std::uint64_t kSeed = 0x5EED5EEDULL;

void testNetwork() {
    const WorldSize world{};
    const valleys::Network n = valleys::makeNetwork(kSeed, world);
    for (const valleys::Octave& o : n.octaves) {
        TEST_CHECK(o.cellsX * o.spacing == world.x && o.cellsZ * o.spacing == world.z);
        TEST_CHECK(o.depth > 0.0f && o.depth < 1.0f);
    }
    TEST_CHECK(n.octaves[0].spacing > n.octaves[1].spacing);
    const valleys::Network small = valleys::makeNetwork(kSeed, WorldSize{65536.0, 196608.0});
    TEST_CHECK(small.octaves[1].cellsX == 4 && small.octaves[1].cellsZ == 12);
}

// The lowest share: every octave on its axis at its full depth.
float lowest(const valleys::Network& n) {
    float low = 1.0f;
    for (const valleys::Octave& o : n.octaves) {
        low *= 1.0f - o.depth;
    }
    return low;
}

void testBoundsPeriodicContinuous() {
    const WorldSize world{};
    const valleys::Network n = valleys::makeNetwork(kSeed, world);
    bool bounded = true;
    bool flat = true;
    double jump = 0.0;
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 0.45 * world.z)) {
        const double down = 0.6;
        const float v = valleys::carve(n, p.x, p.z, 0.8 * 0.01, -down * 0.01, 0.01);
        bounded = bounded && v >= lowest(n) - 1e-6f && v <= 1.0f;
        flat = flat && valleys::carve(n, p.x, p.z, 0.0, 0.0, 0.0) == 1.0f;
        // Across the seams of the world, 1 m apart.
        const float east = valleys::carve(n, 0.5 * world.x - 0.5, p.z, 0.008, -0.006, 0.01);
        const float west = valleys::carve(n, -0.5 * world.x + 0.5, p.z, 0.008, -0.006, 0.01);
        const float north = valleys::carve(n, p.x, 0.5 * world.z - 0.5, 0.008, -0.006, 0.01);
        const float south = valleys::carve(n, p.x, -0.5 * world.z + 0.5, 0.008, -0.006, 0.01);
        jump = std::max({jump, static_cast<double>(std::abs(east - west)),
                         static_cast<double>(std::abs(north - south))});
        const float next = valleys::carve(n, p.x + 1.0, p.z, 0.8 * 0.01, -down * 0.01, 0.01);
        jump = std::max(jump, static_cast<double>(std::abs(next - v)));
    }
    TEST_CHECK(bounded);
    TEST_CHECK(flat);
    TEST_CHECK(jump < 0.01); // 1 m apart: no step
}

// Across the slope (down along +x), valleys about every 32 km: minima of the share over 640 km.
void testSpacing() {
    const valleys::Network n = valleys::makeNetwork(kSeed, WorldSize{});
    int minima = 0;
    float before = valleys::carve(n, 0.0, -1000.0, 0.01, 0.0, 0.01);
    float here = valleys::carve(n, 0.0, 0.0, 0.01, 0.0, 0.01);
    for (int k = 2; k <= 640; ++k) {
        const float after = valleys::carve(n, 0.0, 1000.0 * (k - 1), 0.01, 0.0, 0.01);
        minima += here < before && here <= after && here < 0.5f ? 1 : 0;
        before = here;
        here = after;
    }
    TEST_CHECK(minima >= 8 && minima <= 50);
}

void testDeterminism() {
    const valleys::Network a = valleys::makeNetwork(kSeed, WorldSize{});
    const valleys::Network b = valleys::makeNetwork(kSeed, WorldSize{});
    const valleys::Network c = valleys::makeNetwork(kSeed + 1, WorldSize{});
    int differ = 0;
    for (const bit_exact::Point& p : bit_exact::randomPoints(2'000, 1e7)) {
        TEST_CHECK(bit_exact::same(valleys::carve(a, p.x, p.z, 0.01, 0.0, 0.01),
                                   valleys::carve(b, p.x, p.z, 0.01, 0.0, 0.01)));
        differ += valleys::carve(a, p.x, p.z, 0.01, 0.0, 0.01) !=
                          valleys::carve(c, p.x, p.z, 0.01, 0.0, 0.01)
                      ? 1
                      : 0;
    }
    TEST_CHECK(differ > 1'000);
}

} // namespace

int main() {
    testNetwork();
    testBoundsPeriodicContinuous();
    testSpacing();
    testDeterminism();
    return test::result();
}
