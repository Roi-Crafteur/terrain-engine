// Levels of the far horizon: validation, pitches and coverage (at least 300 km), the window
// centers (even samples near the observer, the same for every image), their continuous moves
// across the edges of the wrapped world, the new samples of a move against a brute-force set
// difference, the toroidal slots, and the nesting of the levels around any observer.
#include "terrain/streaming/horizon_levels.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstdint>
#include <random>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {

using terrain::HorizonIndex;
using terrain::HorizonParams;
using terrain::HorizonRect;

bool throws(const HorizonParams& params) {
    try {
        terrain::validate(params);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

bool throwsWithTiles(const HorizonParams& params, const terrain::LodParams& lod) {
    try {
        terrain::validate(params, lod);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// Deterministic draws for the tests: [0, 1).
class Draws {
public:
    explicit Draws(std::uint64_t seed) : engine_(seed) {}
    double next() { return static_cast<double>(engine_() >> 11) * 0x1.0p-53; }

private:
    std::mt19937_64 engine_;
};

void testValidation() {
    const HorizonParams defaults;
    TEST_CHECK(!throws(defaults));
    for (const int levels : {0, terrain::kHorizonMaxLevels + 1}) {
        HorizonParams params;
        params.levels = levels;
        TEST_CHECK(throws(params));
    }
    for (const int cells : {12, 15, 18, 0, -4}) {
        HorizonParams params;
        params.cellsPerSide = cells;
        TEST_CHECK(throws(params));
    }
    for (const int pitch : {0, 3, 12, -8}) {
        HorizonParams params;
        params.basePitch = pitch;
        TEST_CHECK(throws(params));
        params.basePitch = 8;
        params.materialSlopeStep = pitch;
        TEST_CHECK(throws(params) == (pitch != 0));
    }
    HorizonParams coarse; // 8 m * 2^13 = 65,536 m: two of them do not divide the alignment
    coarse.levels = 14;
    TEST_CHECK(throws(coarse));
    coarse.levels = 13;
    coarse.cellsPerSide = 16; // 17 * 32,768 m: narrower than the default world
    TEST_CHECK(!throws(coarse));

    HorizonParams small; // 257 x 4096 m: wider than a world of 1 024 km
    small.world = {1'048'576.0, 1'048'576.0};
    TEST_CHECK(throws(small));
    small.levels = 9;
    TEST_CHECK(!throws(small));
    small.world = {1'000'000.0, 1'048'576.0};
    TEST_CHECK(throws(small));

    // Level 0 (8 m, 1016 m around the observer) holds the tiles up to 55 chunks.
    terrain::LodParams lod;
    TEST_CHECK(!throwsWithTiles(defaults, lod));
    lod.viewDistance = 55;
    TEST_CHECK(!throwsWithTiles(defaults, lod));
    lod.viewDistance = 56;
    TEST_CHECK(throwsWithTiles(defaults, lod));
    HorizonParams wide;
    wide.cellsPerSide = 512;
    lod.viewDistance = 118;
    TEST_CHECK(!throwsWithTiles(wide, lod));
}

void testPitchesAndCoverage() {
    const HorizonParams params;
    TEST_CHECK(params.levels == 10 && params.basePitch == 8);
    TEST_CHECK(terrain::samplesPerSide(params) == 257);
    for (int level = 0; level < params.levels; ++level) {
        TEST_CHECK(terrain::levelPitch(params, level) == 8 << level);
        TEST_CHECK(terrain::levelRadius(params, level) == 127.0 * (8 << level));
        const HorizonIndex counts = terrain::levelCounts(params, level);
        TEST_CHECK(counts.x % 2 == 0 && counts.z % 2 == 0);
        TEST_CHECK(static_cast<double>(counts.x) * (8 << level) == params.world.x);
        TEST_CHECK(static_cast<double>(counts.z) * (8 << level) == params.world.z);
    }
    TEST_CHECK(terrain::levelPitch(params, params.levels - 1) == 4096);
    TEST_CHECK(terrain::guaranteedRadius(params) == 520'192.0);
    TEST_CHECK(terrain::guaranteedRadius(params) >= 300'000.0);
}

// Distance along an axis between the position v and the nearest image of the sample.
double axisGap(std::int64_t sample, int pitch, double v, double period) {
    const double position = terrain::samplePosition(sample, pitch);
    return std::abs(terrain::nearestImage(position, v, period) - v);
}

void testCenters() {
    const HorizonParams params;
    const terrain::WorldSize world = params.world;
    Draws draws(7);
    for (int i = 0; i < 400; ++i) {
        const double x = (draws.next() - 0.5) * 3.0 * world.x;
        const double z = (draws.next() - 0.5) * 3.0 * world.z;
        for (int level = 0; level < params.levels; ++level) {
            const int pitch = terrain::levelPitch(params, level);
            const HorizonIndex counts = terrain::levelCounts(params, level);
            const HorizonIndex center = terrain::canonicalCenter(params, level, x, z);
            TEST_CHECK(center.x % 2 == 0 && center.z % 2 == 0);
            TEST_CHECK(center == terrain::canonicalSample(center, counts));
            TEST_CHECK(axisGap(center.x, pitch, x, world.x) <= pitch);
            TEST_CHECK(axisGap(center.z, pitch, z, world.z) <= pitch);
            // Every image of the observer gets the same center.
            TEST_CHECK(center == terrain::canonicalCenter(params, level, x + world.x, z - world.z));
        }
    }
    // At the corner of the world, where its four edges meet.
    const HorizonIndex corner = terrain::canonicalCenter(params, 9, world.x / 2.0, world.z / 2.0);
    const HorizonIndex counts = terrain::levelCounts(params, 9);
    TEST_CHECK(corner == (HorizonIndex{-counts.x / 2, -counts.z / 2}));
    bool threw = false;
    try {
        static_cast<void>(terrain::canonicalCenter(params, 0, std::nan(""), 0.0));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
}

void testMoveCenter() {
    const HorizonIndex counts{14'656, 8'544};
    // A small move, as is.
    TEST_CHECK(terrain::moveCenter({10, -4}, {14, -6}, counts) == (HorizonIndex{14, -6}));
    // Across the east edge: the canonical target jumps by a whole world, the center does not.
    const HorizonIndex east{7'326, 0};
    const HorizonIndex moved = terrain::moveCenter(east, {-7'326, 2}, counts);
    TEST_CHECK(moved == (HorizonIndex{7'330, 2}));
    // From a center already several worlds away (a long walk), the move stays small.
    const HorizonIndex far{3 * 14'656 + 100, -2 * 8'544 - 50};
    const HorizonIndex step = terrain::moveCenter(far, {104, -48}, counts);
    TEST_CHECK(step == (HorizonIndex{far.x + 4, far.z + 2}));
    TEST_CHECK(terrain::moveCenter(far, {100, -50}, counts) == far);
}

std::set<std::tuple<std::int64_t, std::int64_t>> samplesOf(const std::vector<HorizonRect>& rects) {
    std::set<std::tuple<std::int64_t, std::int64_t>> samples;
    for (const HorizonRect& rect : rects) {
        for (std::int64_t z = rect.z0; z < rect.z1; ++z) {
            for (std::int64_t x = rect.x0; x < rect.x1; ++x) {
                samples.insert({x, z});
            }
        }
    }
    return samples;
}

void testNewSamples() {
    constexpr int kCells = 16;
    Draws draws(11);
    for (int i = 0; i < 300; ++i) {
        const auto draw = [&draws](int span) {
            return static_cast<std::int64_t>(draws.next() * 2 * span) - span;
        };
        const HorizonIndex a{2 * draw(1000), 2 * draw(1000)};
        const int span = i < 100 ? 3 : (i < 200 ? 10 : 40); // small moves, then jumps
        const HorizonIndex b{a.x + 2 * draw(span), a.z + 2 * draw(span)};
        const HorizonRect from = terrain::windowAround(a, kCells);
        const HorizonRect to = terrain::windowAround(b, kCells);
        const std::vector<HorizonRect> rects = terrain::newSamples(from, to);
        TEST_CHECK(rects.size() <= 2);
        std::int64_t total = 0;
        for (const HorizonRect& rect : rects) {
            TEST_CHECK(!rect.empty());
            total += rect.count();
        }
        const auto fresh = samplesOf(rects);
        TEST_CHECK(static_cast<std::int64_t>(fresh.size()) == total); // disjoint
        std::set<std::tuple<std::int64_t, std::int64_t>> expected;
        for (const auto& sample : samplesOf({to})) {
            if (!from.contains({std::get<0>(sample), std::get<1>(sample)})) {
                expected.insert(sample);
            }
        }
        TEST_CHECK(fresh == expected);
    }
    const HorizonRect window = terrain::windowAround({0, 0}, kCells);
    TEST_CHECK(window == (HorizonRect{-8, -8, 9, 9}));
    TEST_CHECK(terrain::newSamples(window, window).empty());
    TEST_CHECK(terrain::newSamples({}, window) == std::vector<HorizonRect>{window});
}

void testSlots() {
    constexpr int kSide = 17;
    Draws draws(5);
    for (int i = 0; i < 100; ++i) {
        const auto x0 = static_cast<std::int64_t>((draws.next() - 0.5) * 1e6);
        const auto z0 = static_cast<std::int64_t>((draws.next() - 0.5) * 1e6);
        const HorizonRect window{x0, z0, x0 + kSide, z0 + kSide};
        std::set<std::tuple<std::int64_t, std::int64_t>> slots;
        for (std::int64_t z = window.z0; z < window.z1; ++z) {
            for (std::int64_t x = window.x0; x < window.x1; ++x) {
                const HorizonIndex slot = terrain::slotOf({x, z}, kSide);
                TEST_CHECK(slot.x >= 0 && slot.x < kSide && slot.z >= 0 && slot.z < kSide);
                slots.insert({slot.x, slot.z});
            }
        }
        TEST_CHECK(slots.size() == kSide * kSide);

        const HorizonRect part{x0 + 3, z0 + 1, x0 + 3 + (i % kSide) + 1, z0 + 9};
        std::set<std::tuple<std::int64_t, std::int64_t>> expected;
        for (std::int64_t z = part.z0; z < part.z1; ++z) {
            for (std::int64_t x = part.x0; x < part.x1; ++x) {
                const HorizonIndex slot = terrain::slotOf({x, z}, kSide);
                expected.insert({slot.x, slot.z});
            }
        }
        const std::vector<HorizonRect> rects = terrain::slotRects(part, kSide);
        TEST_CHECK(rects.size() <= 4);
        TEST_CHECK(samplesOf(rects) == expected);
    }
}

// Every level lies inside the next coarser one, its borders on samples of it, with a margin of
// at least K / 4 - 1 coarse cells, for observers anywhere (edges and corner of the world too).
void testNesting() {
    const HorizonParams params;
    const terrain::WorldSize world = params.world;
    const int half = params.cellsPerSide / 2;
    Draws draws(3);
    for (int i = 0; i < 300; ++i) {
        double x = (draws.next() - 0.5) * world.x;
        double z = (draws.next() - 0.5) * world.z;
        if (i < 20) {
            x = world.x / 2.0 - 5000.0 + i * 500.0;
            z = -world.z / 2.0 + 300.0 * i;
        }
        for (int level = 0; level + 1 < params.levels; ++level) {
            const int fine = terrain::levelPitch(params, level);
            const int coarse = 2 * fine;
            const HorizonIndex a = terrain::canonicalCenter(params, level, x, z);
            const HorizonIndex b = terrain::canonicalCenter(params, level + 1, x, z);
            for (const auto& [ia, ib, v, period] :
                 {std::tuple{a.x, b.x, x, world.x}, std::tuple{a.z, b.z, z, world.z}}) {
                const double ca =
                    terrain::nearestImage(terrain::samplePosition(ia, fine), v, period);
                const double cb =
                    terrain::nearestImage(terrain::samplePosition(ib, coarse), v, period);
                const double low = ca - half * fine;
                TEST_CHECK(std::fmod(std::abs(low), coarse) == 0.0);
                const double margin = (half * coarse - (std::abs(ca - cb) + half * fine)) / coarse;
                TEST_CHECK(margin >= half / 2 - 1);
            }
        }
    }
}

} // namespace

int main() {
    testValidation();
    testPitchesAndCoverage();
    testCenters();
    testMoveCenter();
    testNewSamples();
    testSlots();
    testNesting();
    return test::result();
}
