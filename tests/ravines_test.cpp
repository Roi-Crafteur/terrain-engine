// The ravines of the mountain slopes of the geography (ravines.hpp): spacings that tile the
// world; depths within their bounds, 0 on the flat and without strength; deterministic,
// continuous (no step 1 m apart, across the seams of the world too); about one ravine per
// spacing across the slope; the same bits through a memo along a sequence of points.
#include "ravines.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using terrain::WorldSize;
namespace ravines = terrain::ravines;

constexpr std::uint64_t kSeed = 0x5EED5EEDULL;

// A slope of 0.5 down along +x: the gradient up the slope.
constexpr double kSlopeX = -0.4;
constexpr double kSlopeZ = 0.3;

void testNetwork() {
    const WorldSize world{};
    const ravines::Network n = ravines::makeNetwork(kSeed, world);
    for (const ravines::Octave& o : n.octaves) {
        TEST_CHECK(o.cellsX * o.spacing == world.x && o.cellsZ * o.spacing == world.z);
        TEST_CHECK(o.depth > 0.0f && o.depth < 1.0f);
    }
    TEST_CHECK(n.octaves[0].spacing > n.octaves[1].spacing);
    const ravines::Network small = ravines::makeNetwork(kSeed, WorldSize{65536.0, 196608.0});
    TEST_CHECK(small.octaves[1].cellsX == 64 && small.octaves[1].cellsZ == 192);
}

// The deepest a ravine can be on a slope: every octave at its full depth.
double deepest(const ravines::Network& n, double slope) {
    double sum = 0.0;
    for (const ravines::Octave& o : n.octaves) {
        sum += static_cast<double>(o.depth) * o.spacing;
    }
    return slope * sum;
}

void testBoundsContinuous() {
    const WorldSize world{};
    const ravines::Network n = ravines::makeNetwork(kSeed, world);
    const double slope = std::hypot(kSlopeX, kSlopeZ);
    bool bounded = true;
    bool none = true;
    double jump = 0.0;
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 0.45 * world.z)) {
        const float d = ravines::depth(n, p.x, p.z, kSlopeX, kSlopeZ, 1.0);
        bounded = bounded && d >= 0.0f && d <= deepest(n, slope) + 1e-3;
        none = none && ravines::depth(n, p.x, p.z, 0.0, 0.0, 1.0) == 0.0f &&
               ravines::depth(n, p.x, p.z, kSlopeX, kSlopeZ, 0.0) == 0.0f;
        // Across the seams of the world, 1 m apart.
        const float east = ravines::depth(n, 0.5 * world.x - 0.5, p.z, kSlopeX, kSlopeZ, 1.0);
        const float west = ravines::depth(n, -0.5 * world.x + 0.5, p.z, kSlopeX, kSlopeZ, 1.0);
        const float north = ravines::depth(n, p.x, 0.5 * world.z - 0.5, kSlopeX, kSlopeZ, 1.0);
        const float south = ravines::depth(n, p.x, -0.5 * world.z + 0.5, kSlopeX, kSlopeZ, 1.0);
        const float next = ravines::depth(n, p.x + 1.0, p.z, kSlopeX, kSlopeZ, 1.0);
        jump = std::max({jump, static_cast<double>(std::abs(east - west)),
                         static_cast<double>(std::abs(north - south)),
                         static_cast<double>(std::abs(next - d))});
    }
    TEST_CHECK(bounded);
    TEST_CHECK(none);
    TEST_CHECK(jump < 3.0); // 1 m apart on a slope of 0.5: steep walls, no step
}

// Across the slope (down along +x, walking along z), deep ravines about every 2 km: maxima of
// the depth above half the deepest over 64 km.
void testSpacing() {
    const ravines::Network n = ravines::makeNetwork(kSeed, WorldSize{});
    const double half = 0.5 * deepest(n, 0.5);
    int beds = 0;
    float before = ravines::depth(n, 0.0, -100.0, -0.5, 0.0, 1.0);
    float here = ravines::depth(n, 0.0, 0.0, -0.5, 0.0, 1.0);
    for (int k = 1; k <= 640; ++k) {
        const float after = ravines::depth(n, 0.0, 100.0 * k, -0.5, 0.0, 1.0);
        beds += here > before && here >= after && here > half ? 1 : 0;
        before = here;
        here = after;
    }
    TEST_CHECK(beds >= 10 && beds <= 64);
}

void testDeterminismAndMemo() {
    const ravines::Network a = ravines::makeNetwork(kSeed, WorldSize{});
    const ravines::Network b = ravines::makeNetwork(kSeed, WorldSize{});
    const ravines::Network c = ravines::makeNetwork(kSeed + 1, WorldSize{});
    ravines::Memo memo;
    int differ = 0;
    bool same = true;
    for (const bit_exact::Point& p : bit_exact::randomPoints(2'000, 1e7)) {
        const float d = ravines::depth(a, p.x, p.z, kSlopeX, kSlopeZ, 1.0);
        same = same && bit_exact::same(d, ravines::depth(b, p.x, p.z, kSlopeX, kSlopeZ, 1.0));
        differ += d != ravines::depth(c, p.x, p.z, kSlopeX, kSlopeZ, 1.0) ? 1 : 0;
        // A row of points through the memo: the bits of the calls without it.
        for (int k = 0; k < 8; ++k) {
            const double x = p.x + 37.0 * k;
            same = same && bit_exact::same(
                               ravines::depth(a, x, p.z, kSlopeX, kSlopeZ, 1.0, &memo),
                               ravines::depth(a, x, p.z, kSlopeX, kSlopeZ, 1.0));
        }
    }
    TEST_CHECK(same);
    TEST_CHECK(differ > 1'000);
}

} // namespace

int main() {
    testNetwork();
    testBoundsContinuous();
    testSpacing();
    testDeterminismAndMemo();
    return test::result();
}
