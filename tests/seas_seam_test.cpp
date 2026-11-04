// The seas across the seams of the world (agent 51, seas.hpp): east and west, north and south
// (the polar edges), the floor of the sea 2 m apart on either side of a seam differs by no more
// than its slope over 2 m, on the small world and on the default one (far from the origin).

#include "seas_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

struct Seam {
    std::size_t sea = 0;
    double worst = 0.0;
};

// The floors of the seas at (a) and (b), 2 m apart across a seam, over the sea of both.
void across(const terrain::Seas& seas, const terrain::Heightfield& relief, double ax, double az,
            double bx, double bz, Seam& out) {
    terrain::HeightSample a = relief.sampleAt(ax, az);
    terrain::HeightSample b = relief.sampleAt(bx, bz);
    if (a.water != terrain::WaterKind::Ocean || b.water != terrain::WaterKind::Ocean) {
        return;
    }
    seas.apply(ax, az, a, 0.0);
    seas.apply(bx, bz, b, 0.0);
    ++out.sea;
    out.worst = std::max(out.worst, std::abs(static_cast<double>(a.height - b.height)));
}

void testWorld(const terrain::Heightfield& relief, const terrain::Seas& seas, const char* name) {
    const terrain::WorldSize& w = relief.world();
    Seam east;
    Seam north;
    for (double t = -0.5; t < 0.5; t += 0.002) {
        across(seas, relief, 0.5 * w.x - 1.0, t * w.z, -0.5 * w.x + 1.0, t * w.z, east);
        across(seas, relief, t * w.x, 0.5 * w.z - 1.0, t * w.x, -0.5 * w.z + 1.0, north);
    }
    std::printf("%s: east-west seam %zu points at sea, %.3f m apart at most; north-south %zu, "
                "%.3f m\n",
                name, east.sea, east.worst, north.sea, north.worst);
    TEST_CHECK(east.sea > 50 && north.sea > 50);
    // A floor falls at most about 0.5 per meter (the flanks of the seamounts, the trenches).
    TEST_CHECK(east.worst < 2.0 && north.worst < 2.0);
}

} // namespace

int main() {
    const terrain::Seas* small = seas_test::worlds().on->seas();
    TEST_CHECK(small != nullptr);
    if (small != nullptr) {
        testWorld(dunes_test::small().relief, *small, "small world");
    }
    const terrain::Heightfield relief(seas_test::kSeed);
    const terrain::Seas real(seas_test::kSeed, relief, nullptr, {}, 0);
    testWorld(relief, real, "default world");
    return test::result();
}
