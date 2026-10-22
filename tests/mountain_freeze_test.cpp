// The mountains of the default world are frozen (phase 11R bis, after step 5 bis): wherever their
// mask is above 0, every bit of the samples stays that of step 5 bis (357d3e9), whatever is done
// to the plains. One checksum per window, over the samples of its points under the mountains
// only: the K2 scene (view_k2), the highest wall near it (view_wall), the foot of the longest
// chain (view_foot, at the fixed place and with the default arguments), and points drawn over the
// whole world. A window that changes prints its new value; it must not be pasted back unless the
// mountains are meant to change.
#include "terrain/generation/heightfield.hpp"

#include "golden_support.hpp"
#include "test_assert.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::HeightSample;

struct Window {
    const char* name;
    double x;    // center, meters
    double z;
    double step; // meters between points
    int side;    // points along each axis
    std::uint64_t expected;
};

void addSample(golden::Checksum& sum, const HeightSample& s) {
    sum.add(s.mountainMask);
    sum.add(s.relief);
    sum.add(s.base);
    sum.add(s.mountain);
    sum.add(s.detail);
    sum.add(s.height);
}

// The samples of the window where the mountain mask is above 0, and how many.
std::uint64_t windowSum(const terrain::Heightfield& field, const Window& w, int& count) {
    golden::Checksum sum;
    count = 0;
    std::vector<double> xs(static_cast<std::size_t>(w.side));
    std::vector<double> zs(static_cast<std::size_t>(w.side));
    std::vector<HeightSample> row(static_cast<std::size_t>(w.side));
    for (int j = 0; j < w.side; ++j) {
        for (int i = 0; i < w.side; ++i) {
            xs[static_cast<std::size_t>(i)] = w.x + w.step * (i - w.side / 2);
            zs[static_cast<std::size_t>(i)] = w.z + w.step * (j - w.side / 2);
        }
        field.samplesAt(xs, zs, row);
        for (const HeightSample& s : row) {
            if (s.mountainMask > 0.0f) {
                addSample(sum, s);
                ++count;
            }
        }
    }
    return sum.value();
}

// Points drawn over the whole world, those under the mountains.
std::uint64_t drawnSum(const terrain::Heightfield& field, int wanted, int& count) {
    golden::Checksum sum;
    count = 0;
    std::uint64_t r = 0x9E3779B97F4A7C15ULL;
    const terrain::WorldSize world{};
    while (count < wanted) {
        r ^= r << 13;
        r ^= r >> 7;
        r ^= r << 17;
        const double u = static_cast<double>(r >> 11) * (1.0 / 9007199254740992.0);
        r ^= r << 13;
        r ^= r >> 7;
        r ^= r << 17;
        const double v = static_cast<double>(r >> 11) * (1.0 / 9007199254740992.0);
        const HeightSample s = field.sampleAt((u - 0.5) * world.x, (v - 0.5) * world.z);
        if (s.mountainMask > 0.0f) {
            addSample(sum, s);
            ++count;
        }
    }
    return sum.value();
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    const Window windows[] = {
        {"k2 scene, 8 km at 25 m", 416'857.0, -11'405'966.0, 25.0, 321, 0x20CC4D545A78840EULL},
        {"highest wall, 1 km at 4 m", 416'000.0, -11'414'800.0, 4.0, 251, 0x39769451019530F1ULL},
        {"foot of the fixed chain, 24 km at 100 m", -27'744'000.0, 4'734'000.0, 100.0, 241, 0x2B61E18AC6E7B1AFULL},
        {"foot of the default chain, 24 km at 100 m", 9'630'245.0, -11'029'895.0, 100.0, 241,
         0x3FD55F5A4448F4C9ULL},
    };
    bool same = true;
    for (const Window& w : windows) {
        int count = 0;
        const std::uint64_t value = windowSum(field, w, count);
        std::printf("{\"%s\", 0x%016" PRIX64 "ULL}, %d points under the mountains%s\n", w.name,
                    value, count, value == w.expected ? "" : "  (differs)");
        same = same && value == w.expected && count > w.side * w.side / 4;
    }
    constexpr std::uint64_t kDrawn = 0xC6812816A4D90C62ULL;
    int count = 0;
    const std::uint64_t drawn = drawnSum(field, 20'000, count);
    std::printf("{\"drawn points\", 0x%016" PRIX64 "ULL}%s\n", drawn,
                drawn == kDrawn ? "" : "  (differs)");
    TEST_CHECK(same);
    TEST_CHECK(drawn == kDrawn);
    return test::result();
}
