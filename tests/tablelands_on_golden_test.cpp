// The default world with the plateaus and the karst switched on (agent 46; off by default until the
// relief of wave 9 is switched on all at once, R1): the checksums of what they make of it, apart
// from the goldens of the world of every day (unchanged: they are off there). And what switching
// them on does to the frozen mountains (mountain_freeze_test), for R1: how many of its points drawn
// under the mountains change, the share of the mountains their zone covers; its four windows stay
// out of it. A sum that changes prints its new value; it must not be pasted back unless the
// plateaus or the karst are meant to change.
#include "coast_zone_support.hpp"
#include "golden_support.hpp"
#include "tablelands_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/pre_climate.hpp"
#include "terrain/generation/tablelands.hpp"

#include <array>
#include <bit>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::HeightSample;
using terrain::Heightfield;

// The share of the points drawn under the mountains that may change when they are switched on
// (the guard of R1: beyond it, the frozen mountains lose too much).
constexpr double kMostFrozenChanged = 0.2;

void addSample(golden::Checksum& sum, const HeightSample& s) {
    for (const float v : {s.mountainMask, s.relief, s.base, s.mountain, s.detail, s.height}) {
        sum.add(v);
    }
}

bool sameBits(const HeightSample& a, const HeightSample& b) {
    const auto bits = [](const HeightSample& s) {
        return std::array<std::uint32_t, 6>{
            std::bit_cast<std::uint32_t>(s.mountainMask), std::bit_cast<std::uint32_t>(s.relief),
            std::bit_cast<std::uint32_t>(s.base), std::bit_cast<std::uint32_t>(s.mountain),
            std::bit_cast<std::uint32_t>(s.detail), std::bit_cast<std::uint32_t>(s.height)};
    };
    return bits(a) == bits(b);
}

// The heights of a lattice of side x side points step meters apart around (x, z).
std::uint64_t latticeSum(const Heightfield& field, double x, double z, double step, int side) {
    golden::Checksum sum;
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            addSample(sum, field.sampleAt(x + step * (i - side / 2), z + step * (j - side / 2)));
        }
    }
    return sum.value();
}

// The points of mountain_freeze_test (its generator, its coastal zone left out) under the
// mountains of the world off: how many change switched on, how many lie in their zone.
void testFrozen(const Heightfield& off, const Heightfield& on) {
    std::uint64_t r = 0x9E3779B97F4A7C15ULL;
    const terrain::WorldSize world{};
    int count = 0;
    int changed = 0;
    int zoned = 0;
    const auto next = [&r] {
        r ^= r << 13;
        r ^= r >> 7;
        r ^= r << 17;
        return static_cast<double>(r >> 11) * (1.0 / 9007199254740992.0);
    };
    while (count < 20'000) {
        const double u = next();
        const double v = next();
        const double x = (u - 0.5) * world.x;
        const double z = (v - 0.5) * world.z;
        if (coast_zone::inZone(coast_zone::coastSignal(off, x, z))) {
            continue;
        }
        const HeightSample s = off.sampleAt(x, z);
        if (!(s.mountainMask > 0.0f)) {
            continue;
        }
        ++count;
        changed += sameBits(s, on.sampleAt(x, z)) ? 0 : 1;
        zoned += on.tablelandAt(x, z).any() ? 1 : 0;
    }
    const double share = static_cast<double>(changed) / static_cast<double>(count);
    std::printf("mountain_freeze_test, switched on: %d of its %d points drawn under the mountains "
                "change (%.2f %%), %d lie in the zone (%.2f %% of the mountains)\n",
                changed, count, 100.0 * share, zoned,
                100.0 * static_cast<double>(zoned) / static_cast<double>(count));
    TEST_CHECK(share <= kMostFrozenChanged);
    // Its windows (mountain_freeze_test): the K2, its wall and the foot of the default chain stay
    // as they are. The foot of the fixed chain lies on a high plateau behind a collision (land at
    // 3,699 m, belt 0.06, the intensity 0.42 of plateauRelief): its surface comes down to the rim
    // range there (0.34), as the high surfaces mean to; R1 decides (stop point 1).
    struct Frozen {
        double x;
        double z;
        double step;
        bool kept;
    };
    const std::array<Frozen, 4> windows = {{{416'857.0, -11'405'966.0, 25.0, true},
                                            {416'000.0, -11'414'800.0, 4.0, true},
                                            {-27'744'000.0, 4'734'000.0, 100.0, false},
                                            {9'630'245.0, -11'029'895.0, 100.0, true}}};
    for (const Frozen& w : windows) {
        const std::uint64_t a = latticeSum(off, w.x, w.z, 8.0 * w.step, 33);
        const std::uint64_t b = latticeSum(on, w.x, w.z, 8.0 * w.step, 33);
        std::printf("window at (%.0f, %.0f): %s\n", w.x, w.z, a == b ? "the same" : "changed");
        TEST_CHECK((a == b) == w.kept);
    }
}

} // namespace

int main() {
    const Heightfield off(terrain::kDefaultWorldSeed);
    const Heightfield& on = tablelands_test::field();
    TEST_CHECK(off.tablelands() == nullptr);
    const terrain::WorldSize world{};
    const std::vector<golden::Part> parts = {
        {"geography map", on.geography()->checksum(), 0xB38C98FC17E1D718ULL},
        {"pre-climate", on.tablelands()->climate().checksum(), 0xECC31F82FD095DCBULL},
        {"provinces", on.tablelands()->checksum(), 0xAFFDD27A321F0ED3ULL},
        {"heights 1000 km", latticeSum(on, 0.0, 0.0, world.z / 32.0, 32), 0x4EB047AFF9FBE6E2ULL},
        {"a high surface, 200 km at 6.25 km", latticeSum(on, 19'554'304.0, -13'131'776.0,
                                                         6'250.0, 32),
         0x809DBFE5CCBED885ULL},
    };
    golden::report("the plateaus and the karst switched on", parts);
    testFrozen(off, on);
    return test::result();
}
