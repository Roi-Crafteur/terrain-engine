// The relief of the floors at the scale of their hills (agent 51, sea_hills.hpp): abyssal hills of
// rms about 1, longer along the axis than across it, the same far from the origin, their fine
// octaves gone under a coarse footprint; fracture zones whose offsets of the axis never jump;
// seamounts under the sea, none on shallow floors, continuous across the seam of the world.

#include "sea_hills.hpp"

#include "test_assert.hpp"

#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

namespace sh = terrain::sea_hills;
constexpr std::uint64_t kSeed = 0x5EA5;

struct Stats {
    double rms = 0.0;
    double across = 0.0; // rms of the change over 200 m across the axis
    double along = 0.0;  // and along it
};

Stats statsAt(const sh::Noises& n, const terrain::SeaParams& p, double u0, double v0) {
    double s2 = 0.0, a2 = 0.0, l2 = 0.0;
    int count = 0;
    for (int j = 0; j < 200; ++j) {
        for (int i = 0; i < 200; ++i) {
            const double u = u0 + 937.0 * i;
            const double v = v0 + 613.0 * j;
            const double h = sh::hillsAt(n, p, u, v, 0.0);
            s2 += h * h;
            const double dv = sh::hillsAt(n, p, u, v + 200.0, 0.0) - h;
            const double du = sh::hillsAt(n, p, u + 200.0, v, 0.0) - h;
            a2 += dv * dv;
            l2 += du * du;
            ++count;
        }
    }
    return {std::sqrt(s2 / count), std::sqrt(a2 / count), std::sqrt(l2 / count)};
}

void testHills() {
    const terrain::SeaParams p;
    const sh::Noises n = sh::makeNoises(kSeed, terrain::WorldSize{}, p);
    const Stats here = statsAt(n, p, 0.0, 0.0);
    const Stats far = statsAt(n, p, 2.9e7, -1.7e7);
    std::printf("hills: rms %.3f (far %.3f), change over 200 m across %.3f, along %.3f\n", here.rms,
                far.rms, here.across, here.along);
    TEST_CHECK(here.rms > 0.8 && here.rms < 1.25);
    TEST_CHECK(std::abs(far.rms - here.rms) < 0.15 * here.rms);
    TEST_CHECK(here.across > 2.0 * here.along);
    // Under a footprint coarser than every octave, their mean: 0; under a fine one, every octave.
    TEST_CHECK(sh::hillsAt(n, p, 1234.0, 5678.0, 5'000.0) == 0.0);
    TEST_CHECK(sh::hillsAt(n, p, 1234.0, 5678.0, 100.0) == sh::hillsAt(n, p, 1234.0, 5678.0, 0.0));
    const double a = sh::hillsAmplitude(n, p, 3.0e6, -2.0e6);
    TEST_CHECK(a >= 1.0 - p.hillsVariation && a <= 1.0 + p.hillsVariation);
}

void testFractures() {
    const terrain::SeaParams p;
    double jump = 0.0;
    double deepest = 0.0;
    int zones = 0;
    bool inTrough = false;
    double last = sh::fractureAt(kSeed, 3, p, -2.5e6).offset;
    for (double u = -2.5e6 + 100.0; u < 2.5e6; u += 100.0) {
        const sh::Fracture f = sh::fractureAt(kSeed, 3, p, u);
        jump = std::max(jump, std::abs(f.offset - last));
        last = f.offset;
        deepest = std::max(deepest, f.trough);
        zones += f.trough > 0.0 && !inTrough ? 1 : 0;
        inTrough = f.trough > 0.0;
        TEST_CHECK(std::abs(f.offset) <= p.transformOffset);
    }
    std::printf("fractures: %d zones over 5,000 km, deepest trough %.0f m, the offset moves by "
                "%.0f m over 100 m at most\n",
                zones, deepest, jump);
    TEST_CHECK(zones >= 8 && zones <= 25);
    TEST_CHECK(deepest <= p.fractureDepthHigh && deepest >= p.fractureDepthLow);
    TEST_CHECK(jump < 2.5 * p.transformOffset * 100.0 / p.transformBlend);
}

void testSeamounts() {
    const terrain::SeaParams p;
    const terrain::WorldSize world;
    const sh::Noises n = sh::makeNoises(kSeed, world, p);
    double highest = 0.0;
    int cellsWith = 0;
    int shallow = 0;
    for (int j = 0; j < 60; ++j) {
        for (int i = 0; i < 60; ++i) {
            const double x = -3.0e6 + 8'000.0 * i;
            const double z = 1.0e6 + 8'000.0 * j;
            const double h = sh::seamountsAt(n, kSeed, world, p, x, z, -4'500.0);
            highest = std::max(highest, h);
            cellsWith += h > 100.0 ? 1 : 0;
            TEST_CHECK(-4'500.0 + h <= -p.summitDepth);
            shallow += sh::seamountsAt(n, kSeed, world, p, x, z, -1'000.0) > 0.0 ? 1 : 0;
        }
    }
    std::printf("seamounts: %d of 3,600 points over 100 m, highest %.0f m\n", cellsWith, highest);
    TEST_CHECK(cellsWith > 20 && highest > 1'000.0 && highest <= p.seamountHigh);
    TEST_CHECK(shallow == 0);
    // Across the seam of the world, 2 m apart: the same cone.
    double worst = 0.0;
    for (double z = -1.7e7; z < 1.7e7; z += 1'000.0) {
        const double a = sh::seamountsAt(n, kSeed, world, p, 0.5 * world.x - 1.0, z, -5'000.0);
        const double b = sh::seamountsAt(n, kSeed, world, p, -0.5 * world.x + 1.0, z, -5'000.0);
        worst = std::max(worst, std::abs(a - b));
    }
    std::printf("seamounts across the seam: %.3f m apart at most\n", worst);
    TEST_CHECK(worst < 2.0);
}

} // namespace

int main() {
    testHills();
    testFractures();
    testSeamounts();
    return test::result();
}
