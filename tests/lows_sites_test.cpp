// The sites of the lows of agent 52 (lows.hpp) on the default world, the lows on:
// - basins and grabens, the depressions of phase 11B among the lows; the same sites on every
//   build, Heightfield::lowsIn sorted and the same whatever the boxes asked;
// - Heightfield::mayBeDryBelowSea true at the center of every low;
// - the same code with the lows on and off: every sample out of their reach keeps every bit
//   (random points over the world), and their floors go down below the sea level.
#include "terrain/generation/heightfield.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::HeightSample;
using terrain::LowKind;
using terrain::LowSite;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

terrain::HeightfieldParams paramsWith(bool lows) {
    terrain::HeightfieldParams p;
    p.lows.enabled = lows;
    return p;
}

const Heightfield& on() {
    static const Heightfield f(kSeed, paramsWith(true));
    return f;
}

const Heightfield& off() {
    static const Heightfield f(kSeed, paramsWith(false));
    return f;
}

std::vector<LowSite> all(const Heightfield& f) {
    const terrain::WorldSize& w = f.world();
    return f.lowsIn(-0.5 * w.x, -0.5 * w.z, 0.5 * w.x, 0.5 * w.z);
}

void testSites() {
    const std::vector<LowSite> sites = all(on());
    std::size_t count[terrain::kLowKindCount] = {};
    for (const LowSite& s : sites) {
        ++count[static_cast<int>(s.kind)];
    }
    std::printf("%zu depressions, %zu basins, %zu grabens\n", count[0], count[1], count[2]);
    TEST_CHECK(count[0] >= 20 && count[1] >= 100 && count[2] >= 1);
    TEST_CHECK(std::is_sorted(sites.begin(), sites.end(), [](const LowSite& a, const LowSite& b) {
        return a.x < b.x || (a.x == b.x && a.z < b.z);
    }));
    // The same sites cut in two boxes along x.
    const terrain::WorldSize& w = on().world();
    std::vector<LowSite> halves = on().lowsIn(-0.5 * w.x, -0.5 * w.z, 0.0, 0.5 * w.z);
    const std::vector<LowSite> east = on().lowsIn(0.0, -0.5 * w.z, 0.5 * w.x, 0.5 * w.z);
    halves.insert(halves.end(), east.begin(), east.end());
    TEST_CHECK(halves == sites);
    // Off: the depressions alone.
    TEST_CHECK(all(off()).size() == count[0]);
    for (const LowSite& s : sites) {
        TEST_CHECK(on().mayBeDryBelowSea(s.x, s.z));
        TEST_CHECK(s.floor < 0.0f && s.reach > s.radius);
    }
}

bool sameSample(const HeightSample& a, const HeightSample& b) {
    return bit_exact::same(a.base, b.base) && bit_exact::same(a.height, b.height) &&
           bit_exact::same(a.detail, b.detail) && bit_exact::same(a.regional, b.regional) &&
           a.water == b.water;
}

// Out of the reach of the lows, the same bits on and off; at the centers of the basins and
// grabens, lower ground, below the sea level at most of them.
void testOutside() {
    std::size_t outside = 0;
    std::size_t changed = 0;
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 3e7)) {
        if (on().mayBeDryBelowSea(p.x, p.z)) {
            continue;
        }
        ++outside;
        changed += sameSample(on().sampleAt(p.x, p.z), off().sampleAt(p.x, p.z)) ? 0 : 1;
    }
    std::printf("%zu points out of the reach of the lows, %zu changed\n", outside, changed);
    TEST_CHECK(outside > 15'000 && changed == 0);
    std::size_t lowered = 0;
    std::size_t below = 0;
    std::size_t lows = 0;
    for (const LowSite& s : all(on())) {
        if (s.kind == LowKind::Depression) {
            continue;
        }
        ++lows;
        const float h = on().heightAt(s.x, s.z);
        lowered += h < off().heightAt(s.x, s.z) ? 1 : 0;
        below += h < terrain::kSeaLevel ? 1 : 0;
    }
    std::printf("%zu of %zu lows lower at their center, %zu below the sea level\n", lowered, lows,
                below);
    TEST_CHECK(lowered * 10 >= lows * 9 && below * 2 >= lows);
}

} // namespace

int main() {
    testSites();
    testOutside();
    return test::result();
}
