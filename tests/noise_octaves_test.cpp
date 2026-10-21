// FbmOctaves gives exactly the bits of fbm2D, ridged2D and fbm3D with the same seed, parameters
// and world, for any parameters: no octave or a negative count, more octaves than rotations,
// another lacunarity, a negative or zero gain, a total amplitude of 0; on pseudo-random points,
// on the edges and the corner of the world, at -0, and far beyond it; and so do they through a
// memo, along sequences of points (rows across the edges, scattered points), and as pairs of
// twins (fbm2DPair, the same octaves with another seed). wrapCanonical gives
// exactly the bits of wrapCoordinate, on both sides of both bounds of the canonical range. The
// lattices of phase 11R (RotatedFast, Unrotated) differ from the default only as documented.
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

namespace noise = terrain::noise;
using bit_exact::Point;
using bit_exact::same;
using bit_exact::Tally;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = 4242;

constexpr auto kFast = noise::OctaveLattices::RotatedFast;
constexpr auto kUnrotated = noise::OctaveLattices::Unrotated;

const std::array<noise::FbmParams, 13> kParams = {{
    {.octaves = -1, .frequency = 1.0 / 900.0},
    {.octaves = 0, .frequency = 1.0 / 900.0},
    {.octaves = 1, .frequency = 1.0 / 60.0},
    {.octaves = 8, .frequency = 1.0 / 30000.0},
    {.octaves = 9, .frequency = 1.0 / 5000.0, .lacunarity = 1.9, .gain = 0.55},
    {.octaves = 12, .frequency = 1.0 / 2e6, .lacunarity = 2.5, .gain = 0.45},
    {.octaves = 3, .frequency = 1.0 / 700.0, .gain = -0.4},
    {.octaves = 4, .frequency = 1.0 / 700.0, .gain = 0.0},
    {.octaves = 2, .frequency = 1.0 / 700.0, .gain = -1.0}, // total amplitude 0
    {.octaves = 4, .frequency = 1.0 / 60000.0},
    {.octaves = 9, .frequency = 1.0 / 5000.0, .gain = 0.55, .lattices = kFast},
    {.octaves = 10, .frequency = 1.0 / 2e6, .lattices = kUnrotated},
    {.octaves = 3, .frequency = 1.0 / 700.0, .lacunarity = 1.9, .lattices = kUnrotated},
}};

void checkPoint(Tally& tally, const noise::FbmParams& params, const WorldSize& world,
                const noise::FbmOctaves& octaves, Point p) {
    const double y = 0.29 * p.z - 0.53 * p.x;
    tally.check(same(octaves.fbm2D(p.x, p.z), noise::fbm2D(p.x, p.z, kSeed, params, world)), p,
                "fbm2D");
    for (const double sharpness : {1.0, 2.0, 2.5, 4.0}) {
        tally.check(same(octaves.ridged2D(p.x, p.z, sharpness),
                         noise::ridged2D(p.x, p.z, kSeed, params, world, sharpness)),
                    p, "ridged2D");
    }
    tally.check(same(octaves.fbm3D(p.x, y, p.z), noise::fbm3D(p.x, y, p.z, kSeed, params, world)),
                p, "fbm3D");
}

// The memoized fbm2D and ridged2D of a sequence of points: the bits of the free functions.
void checkMemo(Tally& tally, const noise::FbmParams& params, const WorldSize& world,
               const noise::FbmOctaves& octaves, const std::vector<Point>& points) {
    noise::FbmOctaves::Memo fbmMemo;
    noise::FbmOctaves::Memo ridgedMemo;
    for (const Point& p : points) {
        tally.check(same(octaves.fbm2D(p.x, p.z, fbmMemo),
                         noise::fbm2D(p.x, p.z, kSeed, params, world)),
                    p, "fbm2D with a memo");
        tally.check(same(octaves.ridged2D(p.x, p.z, 4.0, ridgedMemo),
                         noise::ridged2D(p.x, p.z, kSeed, params, world, 4.0)),
                    p, "ridged2D with a memo");
    }
}

// A twin (another seed) through fbm2DPair, straight and through memos: the bits of the two
// fbm2D.
void checkTwins(Tally& tally, const noise::FbmParams& params, const WorldSize& world,
                const noise::FbmOctaves& octaves, const std::vector<Point>& points) {
    const noise::FbmOctaves twin(kSeed + 1, params, world);
    TEST_CHECK(octaves.twinOf(twin) && twin.twinOf(octaves));
    noise::FbmOctaves::Memo memo;
    noise::FbmOctaves::Memo twinMemo;
    for (const Point& p : points) {
        const float a = octaves.fbm2D(p.x, p.z);
        const float b = twin.fbm2D(p.x, p.z);
        const std::array<float, 2> straight = octaves.fbm2DPair(twin, p.x, p.z);
        const std::array<float, 2> memoized = octaves.fbm2DPair(twin, p.x, p.z, memo, twinMemo);
        tally.check(same(straight[0], a) && same(straight[1], b), p, "fbm2DPair");
        tally.check(same(memoized[0], a) && same(memoized[1], b), p, "fbm2DPair with memos");
    }
}

void checkFreeFunctions() {
    Tally tally("FbmOctaves == free functions");
    const std::vector<Point> random = bit_exact::randomPoints(5'000, 4e7);
    const std::vector<Point> near = bit_exact::randomPoints(2'000, 500.0);
    for (const WorldSize& world : bit_exact::kWorlds) {
        const std::vector<Point> edges = bit_exact::edgePoints(world);
        for (const noise::FbmParams& params : kParams) {
            const noise::FbmOctaves octaves(kSeed, params, world);
            TEST_CHECK(octaves.octaves() == std::max(params.octaves, 0));
            for (const auto* set : {&random, &near, &edges}) {
                for (const Point& p : *set) {
                    checkPoint(tally, params, world, octaves, p);
                }
                checkMemo(tally, params, world, octaves, *set);
            }
            checkMemo(tally, params, world, octaves, bit_exact::rowPoints(world, 300));
            checkTwins(tally, params, world, octaves, bit_exact::rowPoints(world, 100));
            checkTwins(tally, params, world, octaves, edges);
        }
    }
}

// Twins differ by their seed only: another frequency (beyond the adjustment of the lattices),
// world, gain or count of octaves is not a twin.
void checkTwinOf() {
    const noise::FbmParams params{.octaves = 3, .frequency = 1.0 / 15000.0};
    const noise::FbmOctaves octaves(kSeed, params, WorldSize{});
    TEST_CHECK(octaves.twinOf(noise::FbmOctaves(kSeed + 7, params, WorldSize{})));
    noise::FbmParams other = params;
    other.frequency = 1.0 / 16000.0; // 1 / 15001 rounds to the same lattices: twins indeed
    TEST_CHECK(!octaves.twinOf(noise::FbmOctaves(kSeed, other, WorldSize{})));
    other = params;
    other.octaves = 4;
    TEST_CHECK(!octaves.twinOf(noise::FbmOctaves(kSeed, other, WorldSize{})));
    other = params;
    other.gain = 0.6;
    TEST_CHECK(!octaves.twinOf(noise::FbmOctaves(kSeed, other, WorldSize{})));
    TEST_CHECK(!octaves.twinOf(noise::FbmOctaves(kSeed, params, bit_exact::kWorlds[1])));
}

// The lattices of phase 11R: RotatedFast differs from Rotated by roundings only; Unrotated has
// no rotation, and its octaves do not share the zeros of their lattices.
void checkLatticeModes() {
    const WorldSize world;
    noise::FbmParams exact{.octaves = 7, .frequency = 1.0 / 24000.0};
    noise::FbmParams fast = exact;
    fast.lattices = kFast;
    noise::FbmParams unrotated = exact;
    unrotated.lattices = kUnrotated;
    double worst = 0.0;
    int differing = 0;
    for (const Point& p : bit_exact::randomPoints(20'000, 3e7)) {
        const float a = noise::fbm2D(p.x, p.z, kSeed, exact, world);
        const float b = noise::fbm2D(p.x, p.z, kSeed, fast, world);
        worst = std::max(worst, static_cast<double>(std::abs(a - b)));
        differing += same(a, b) ? 0 : 1;
    }
    TEST_CHECK(worst <= 1.0e-6);
    TEST_CHECK(differing > 0); // other bits indeed
    for (int octave = 0; octave < 12; ++octave) {
        const noise::Lattice2D rotated = noise::octaveLattice(world, 1e-4, octave, kFast);
        const noise::Lattice2D plain = noise::octaveLattice(world, 1e-4, octave, kUnrotated);
        TEST_CHECK(plain.c == 1 && !plain.reciprocal);
        TEST_CHECK(rotated.reciprocal == (rotated.c != 1));
        TEST_CHECK((octave == 0) == (plain.offsetU == 0.0 && plain.offsetV == 0.0));
        const noise::Lattice2D same2D = noise::octaveLattice(world, 1e-4, octave,
                                                             noise::OctaveLattices::Rotated);
        const noise::Lattice2D reference = noise::lattice2D(world, 1e-4, octave);
        TEST_CHECK(same2D.c == reference.c && same(same2D.scaleX, reference.scaleX) &&
                   !same2D.reciprocal);
    }
    // Every octave of Rotated is 0 at the origin; Unrotated moves all but the first.
    TEST_CHECK(noise::fbm2D(0.0, 0.0, kSeed, exact, world) == 0.0f);
    TEST_CHECK(noise::fbm2D(0.0, 0.0, kSeed, unrotated, world) != 0.0f);
    const noise::FbmOctaves a(kSeed, exact, world);
    TEST_CHECK(!a.twinOf(noise::FbmOctaves(kSeed, fast, world)));
    TEST_CHECK(!a.twinOf(noise::FbmOctaves(kSeed, unrotated, world)));
    TEST_CHECK(noise::FbmOctaves(kSeed, fast, world).twinOf(noise::FbmOctaves(1, fast, world)));
}

void checkEmpty() {
    const noise::FbmOctaves none;
    TEST_CHECK(none.octaves() == 0);
    TEST_CHECK(same(none.fbm2D(12.5, -3.0), 0.0f));
    TEST_CHECK(same(none.ridged2D(12.5, -3.0, 2.0), 0.0f));
    TEST_CHECK(same(none.fbm3D(12.5, 7.0, -3.0), 0.0f));
    // A total amplitude of 0: the free functions give +0 too.
    const noise::FbmParams cancelled{.octaves = 2, .frequency = 0.01, .gain = -1.0};
    const noise::FbmOctaves zero(kSeed, cancelled, WorldSize{});
    TEST_CHECK(same(zero.fbm2D(40.0, 2.0), 0.0f));
    TEST_CHECK(same(noise::fbm2D(40.0, 2.0, kSeed, cancelled, WorldSize{}), 0.0f));
}

// Values around the bounds +-period / 2 (8 neighbors on each side), around 0 and the periods,
// and special values.
std::vector<double> wrapValues(double period) {
    using limits = std::numeric_limits<double>;
    const double half = 0.5 * period;
    std::vector<double> values = {0.0,           -0.0,          limits::denorm_min(),
                                  -limits::min(), limits::max(), -limits::max(),
                                  limits::infinity(), -limits::infinity(), limits::quiet_NaN()};
    for (const double center : {half, -half, 0.0, period, -period, 1.5 * period, 1e9 * period}) {
        double below = center;
        double above = center;
        values.push_back(center);
        for (int step = 0; step < 8; ++step) {
            below = std::nextafter(below, -limits::infinity());
            above = std::nextafter(above, limits::infinity());
            values.push_back(below);
            values.push_back(above);
        }
    }
    bit_exact::Points points;
    for (int i = 0; i < 20'000; ++i) {
        values.push_back(points.next(3.0 * period));
    }
    return values;
}

void checkWrapCanonical() {
    Tally tally("wrapCanonical == wrapCoordinate");
    for (const double period : {WorldSize{}.x, WorldSize{}.z, 65536.0, 3.0 * 65536.0, 0x1p30}) {
        for (const double v : wrapValues(period)) {
            const double fast = noise::wrapCanonical(v, period);
            const double slow = terrain::wrapCoordinate(v, period);
            tally.check(same(fast, slow) || (std::isnan(fast) && std::isnan(slow)), {v, period},
                        "wrapCanonical");
        }
    }
}

} // namespace

int main() {
    checkFreeFunctions();
    checkEmpty();
    checkTwinOf();
    checkLatticeModes();
    checkWrapCanonical();
    return test::result();
}
