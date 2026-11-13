// The hollows of the plains of agent 52 (plain_hollows.hpp) on the default world, the lows on:
// - Heightfield::hollowsIn the same whatever the boxes asked, every center on a plain far from
//   the sea, Heightfield::mayBeDryBelowSea true there;
// - their kinds by the pre-climate at their center: kettles only where it is cold and not dry,
//   about 1.3 per square kilometer in the heart of their belt; pans mostly in the dry lands, about
//   one per 400 square kilometers there, ten times fewer elsewhere at least;
// - the same code with the hollows on and off: they never raise the ground; out of their
//   outlines every sample keeps every bit; their centers go down by most of their depth.
#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/pre_climate.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::HeightSample;
using terrain::LowKind;
using terrain::LowSite;

const Heightfield& withHollows(bool hollows) {
    static const auto make = [](bool on) {
        terrain::HeightfieldParams p;
        p.lows.enabled = true;
        p.lows.hollows = on;
        return Heightfield(terrain::kDefaultWorldSeed, p);
    };
    static const Heightfield yes = make(true);
    static const Heightfield no = make(false);
    return hollows ? yes : no;
}

const Heightfield& field() {
    return withHollows(true);
}

const terrain::PreClimate& climate() {
    static const terrain::PreClimate c(*field().geography());
    return c;
}

enum Belt { kColdWet, kArid, kElsewhere, kBelts };

Belt beltAt(double x, double z) {
    const float t = climate().temperatureAt(x, z);
    const float h = climate().humidityAt(x, z);
    return t < 3.0f && h > 0.3f ? kColdWet : h < 0.15f ? kArid : kElsewhere;
}

// Random centers of windows on the plains far from the sea, as many per belt.
std::vector<std::vector<std::array<double, 2>>> windows(std::size_t perBelt) {
    std::vector<std::vector<std::array<double, 2>>> out(kBelts);
    const terrain::WorldSize& w = field().world();
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> ux(-0.5 * w.x, 0.5 * w.x);
    std::uniform_real_distribution<double> uz(-0.4 * w.z, 0.4 * w.z);
    for (int n = 0; n < 20'000; ++n) {
        const double x = ux(rng);
        const double z = uz(rng);
        const terrain::GeographySample g = field().geography()->sampleAt(x, z);
        auto& list = out[beltAt(x, z)];
        if (g.coast > 150'000.0 && g.relief <= 0.11f && list.size() < perBelt) {
            list.push_back({x, z});
        }
    }
    return out;
}

void testSites() {
    const double x = -18'791'136.0;
    const double z = -11'814'938.0;
    const std::vector<LowSite> all = field().hollowsIn(x - 20'000.0, z, x + 20'000.0, z + 20'000.0);
    std::vector<LowSite> halves = field().hollowsIn(x - 20'000.0, z, x, z + 20'000.0);
    const std::vector<LowSite> east = field().hollowsIn(x, z, x + 20'000.0, z + 20'000.0);
    halves.insert(halves.end(), east.begin(), east.end());
    std::size_t same = 0;
    for (const LowSite& s : all) {
        same += std::count(halves.begin(), halves.end(), s) == 1 ? 1 : 0;
    }
    TEST_CHECK(halves.size() == all.size() && same == all.size());
    for (const LowSite& s : all) {
        const terrain::GeographySample g = field().geography()->sampleAt(s.x, s.z);
        TEST_CHECK(g.relief <= 0.11f && g.coast >= 40'000.0 && g.belt <= 0.0f);
        TEST_CHECK(field().mayBeDryBelowSea(s.x, s.z));
        TEST_CHECK(s.kind == LowKind::Kettle || s.kind == LowKind::Pan);
    }
    TEST_CHECK(withHollows(false).hollowsIn(x - 20'000.0, z, x + 20'000.0, z + 20'000.0).empty());
}

void testBelts() {
    constexpr double kKettleSide = 5'000.0;
    constexpr double kPanSide = 100'000.0;
    std::array<double, kBelts> kettles{};
    std::array<double, kBelts> pans{};
    std::array<std::size_t, kBelts> count{};
    std::size_t wrongKettles = 0;
    const auto all = windows(60);
    for (std::size_t b = 0; b < kBelts; ++b) {
        for (const auto& [x, z] : all[b]) {
            ++count[b];
            for (const LowSite& s : field().hollowsIn(x - kKettleSide, z - kKettleSide,
                                                      x + kKettleSide, z + kKettleSide)) {
                kettles[b] += s.kind == LowKind::Kettle ? 1.0 : 0.0;
                const bool cold = climate().temperatureAt(s.x, s.z) < 8.0f &&
                                  climate().humidityAt(s.x, s.z) > 0.2f;
                wrongKettles += s.kind == LowKind::Kettle && !cold ? 1 : 0;
            }
            for (const LowSite& s :
                 field().hollowsIn(x - kPanSide, z - kPanSide, x + kPanSide, z + kPanSide)) {
                pans[b] += s.kind == LowKind::Pan ? 1.0 : 0.0;
            }
        }
        kettles[b] /= static_cast<double>(count[b]) * 4.0 * kKettleSide * kKettleSide / 1e6;
        pans[b] /= static_cast<double>(count[b]) * 4.0 * kPanSide * kPanSide / 1e9;
    }
    std::printf("kettles per km2: cold %.2f, arid %.2f, elsewhere %.2f (%zu, %zu, %zu windows)\n",
                kettles[kColdWet], kettles[kArid], kettles[kElsewhere], count[kColdWet],
                count[kArid], count[kElsewhere]);
    std::printf("pans per 1000 km2: cold %.2f, arid %.2f, elsewhere %.2f\n", pans[kColdWet],
                pans[kArid], pans[kElsewhere]);
    TEST_CHECK(count[kColdWet] >= 30 && count[kArid] >= 30 && count[kElsewhere] >= 30);
    TEST_CHECK(wrongKettles == 0 && kettles[kArid] == 0.0);
    TEST_CHECK(kettles[kColdWet] > 0.8 && kettles[kColdWet] < 2.0);
    TEST_CHECK(kettles[kElsewhere] < 0.2 * kettles[kColdWet]);
    TEST_CHECK(pans[kArid] > 1.5 && pans[kArid] < 4.0);
    TEST_CHECK(pans[kColdWet] < 0.1 * pans[kArid] && pans[kElsewhere] < 0.3 * pans[kArid]);
}

// The sample at (x, z) with the hollows on and off: compared into counts.
struct Counts {
    std::size_t changed = 0;
    std::size_t same = 0;
    std::size_t higher = 0;
    std::size_t stray = 0; // changed where no hollow lies (mayBeDryBelowSea false)
};
void compare(double x, double z, Counts& c) {
    const HeightSample a = field().sampleAt(x, z);
    const HeightSample b = withHollows(false).sampleAt(x, z);
    const bool same = bit_exact::same(a.height, b.height) && bit_exact::same(a.base, b.base) &&
                      bit_exact::same(a.regional, b.regional) &&
                      bit_exact::same(a.detail, b.detail);
    c.higher += a.height > b.height ? 1 : 0;
    c.changed += same ? 0 : 1;
    c.same += same ? 1 : 0;
    c.stray += !same && !field().mayBeDryBelowSea(x, z) ? 1 : 0;
}

// On and off over grids of windows and of the hollows in them: never higher; out of their
// outlines the same bits; their centers down.
void testGround() {
    const auto all = windows(6);
    Counts c;
    std::size_t deep = 0;
    std::size_t centers = 0;
    for (const auto& belt : all) {
        for (const auto& [x, z] : belt) {
            for (const LowSite& s : field().hollowsIn(x - 3'000.0, z - 3'000.0, x + 3'000.0,
                                                      z + 3'000.0)) {
                ++centers;
                const float drop =
                    withHollows(false).heightAt(s.x, s.z) - field().heightAt(s.x, s.z);
                deep += drop > 0.5f * -s.floor ? 1 : 0;
                for (int j = -4; j <= 4; ++j) {
                    for (int i = -4; i <= 4; ++i) {
                        compare(s.x + 0.3 * i * s.reach, s.z + 0.3 * j * s.reach, c);
                    }
                }
            }
            for (int j = 0; j < 20; ++j) {
                for (int i = 0; i < 20; ++i) {
                    compare(x - 3'000.0 + 300.0 * i + 0.25, z - 3'000.0 + 300.0 * j + 0.75, c);
                }
            }
        }
    }
    std::printf("%zu samples changed, %zu the same; higher %zu; changed out of the hollows %zu; "
                "%zu of %zu centers down by half their depth at least\n",
                c.changed, c.same, c.higher, c.stray, deep, centers);
    TEST_CHECK(c.changed > 5'000 && c.same > 5'000);
    TEST_CHECK(c.higher == 0 && c.stray == 0);
    TEST_CHECK(centers > 50 && deep * 10 >= centers * 9);
}

} // namespace

int main() {
    testSites();
    testBelts();
    testGround();
    return test::result();
}
