#include "terrain/generation/noise.hpp"

#include "test_assert.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

namespace noise = terrain::noise;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = 42;
const WorldSize kWorld;
// One lattice cell per meter, unrotated: its points are the integer coordinates.
const noise::Lattice2D kUnit = noise::lattice2D(kWorld, 1.0);
constexpr noise::FbmParams kFbm{.octaves = 5, .frequency = 0.37};
constexpr noise::FbmParams kFbm3{.octaves = 5, .frequency = 0.05};

// Deterministic pseudo-random coordinates in [-range, range).
class Points {
public:
    explicit Points(std::uint64_t seed) : state_(seed) {}

    double next(double range) {
        state_ = noise::deriveSeed(state_, 0);
        const double unit = static_cast<double>(state_ >> 11) / 9007199254740992.0; // 2^53
        return (2.0 * unit - 1.0) * range;
    }

private:
    std::uint64_t state_;
};

float gradient(double x, double z, std::uint64_t seed = kSeed) {
    return noise::gradient2D(x, z, seed, kUnit);
}

float gradient3(double x, double y, double z, std::uint64_t seed = kSeed) {
    return noise::gradient3D(x, y, z, seed, kUnit, 1.0);
}

float fbm(double x, double z, std::uint64_t seed = kSeed, const noise::FbmParams& p = kFbm) {
    return noise::fbm2D(x, z, seed, p, kWorld);
}

float ridged(double x, double z, double sharpness = 2.0) {
    return noise::ridged2D(x, z, kSeed, kFbm, kWorld, sharpness);
}

void testDeterminism() {
    Points points(1);
    for (int i = 0; i < 1000; ++i) {
        const double x = points.next(1.0e5);
        const double z = points.next(1.0e5);
        TEST_CHECK(gradient(x, z) == gradient(x, z));
        TEST_CHECK(fbm(x, z) == fbm(x, z));
        TEST_CHECK(ridged(x, z) == ridged(x, z));
    }
    TEST_CHECK(noise::deriveSeed(kSeed, 3) == noise::deriveSeed(kSeed, 3));
    TEST_CHECK(noise::deriveSeed(kSeed, 3) != noise::deriveSeed(kSeed, 4));
    TEST_CHECK(noise::deriveSeed(kSeed, 3) != noise::deriveSeed(kSeed + 1, 3));
}

void testZeroOnLattice() {
    for (int z = -20; z <= 20; ++z) {
        for (int x = -20; x <= 20; ++x) {
            TEST_CHECK(gradient(x, z) == 0.0f);
        }
    }
    TEST_CHECK(gradient(-123456789.0, 987654321.0) == 0.0f); // beyond the world, wrapped
}

void testRanges() {
    Points points(2);
    float low = 0.0f;
    float high = 0.0f;
    bool inRange = true;
    for (int i = 0; i < 100000; ++i) {
        const double x = points.next(1.0e4);
        const double z = points.next(1.0e4);
        const float g = gradient(x, z);
        const float f = fbm(x, z);
        const float r = ridged(x, z);
        inRange = inRange && g >= -1.0f && g <= 1.0f && f >= -1.0f && f <= 1.0f && r >= 0.0f &&
                  r <= 1.0f;
        low = std::fmin(low, g);
        high = std::fmax(high, g);
    }
    TEST_CHECK(inRange);
    // The noise uses most of its range.
    TEST_CHECK(low < -0.6f);
    TEST_CHECK(high > 0.6f);
}

void testContinuity() {
    Points points(3);
    constexpr double kStep = 1.0e-4;
    bool continuous = true;
    for (int i = 0; i < 10000; ++i) {
        const double x = points.next(1.0e3);
        const double z = points.next(1.0e3);
        const float g = gradient(x, z);
        const float f = fbm(x, z);
        const float r = ridged(x, z);
        // Slopes stay below a few units per lattice cell, octave frequencies included.
        continuous = continuous && std::abs(gradient(x + kStep, z) - g) < 1.0e-3f &&
                     std::abs(gradient(x, z + kStep) - g) < 1.0e-3f &&
                     std::abs(fbm(x + kStep, z) - f) < 1.0e-2f &&
                     std::abs(ridged(x, z + kStep) - r) < 1.0e-2f;
    }
    TEST_CHECK(continuous);
}

void testSeedsDiffer() {
    Points points(4);
    int different = 0;
    constexpr int kCount = 1000;
    for (int i = 0; i < kCount; ++i) {
        const double x = points.next(1.0e3);
        const double z = points.next(1.0e3);
        if (gradient(x, z) != gradient(x, z, kSeed + 1)) {
            ++different;
        }
    }
    TEST_CHECK(different > kCount * 9 / 10);
}

void testRidgedSharpness() {
    // Every octave of a sharper ridge is lower, and so is its weight on the next octave.
    Points points(5);
    bool lower = true;
    for (int i = 0; i < 10000; ++i) {
        const double x = points.next(1.0e3);
        const double z = points.next(1.0e3);
        lower = lower && ridged(x, z, 4.0) <= ridged(x, z, 2.0);
    }
    TEST_CHECK(lower);
}

void testNoOctaves() {
    const noise::FbmParams none{.octaves = 0};
    TEST_CHECK(noise::fbm2D(0.3, 0.7, kSeed, none, kWorld) == 0.0f);
    TEST_CHECK(noise::ridged2D(0.3, 0.7, kSeed, none, kWorld) == 0.0f);
    TEST_CHECK(noise::fbm3D(0.3, 0.5, 0.7, kSeed, none, kWorld) == 0.0f);
}

void testSmoothstep() {
    TEST_CHECK(noise::smoothstep(0.0f, 1.0f, -5.0f) == 0.0f);
    TEST_CHECK(noise::smoothstep(0.0f, 1.0f, 0.0f) == 0.0f);
    TEST_CHECK(noise::smoothstep(0.0f, 1.0f, 0.5f) == 0.5f);
    TEST_CHECK(noise::smoothstep(0.0f, 1.0f, 1.0f) == 1.0f);
    TEST_CHECK(noise::smoothstep(0.0f, 1.0f, 7.0f) == 1.0f);
    TEST_CHECK(noise::smoothstep(-2.0f, 2.0f, 0.0f) == 0.5f);
    TEST_CHECK(noise::smoothstep(0.0f, 1.0f, 0.25f) < noise::smoothstep(0.0f, 1.0f, 0.3f));
}

// Frozen outputs of the noises (printed with %.9g, exact for a float), the same on every
// platform.
void testFrozen() {
    constexpr std::uint64_t kFrozenSeed = 20261019;
    constexpr noise::FbmParams kFrozenFbm{
        .octaves = 4, .frequency = 0.01, .lacunarity = 2.0, .gain = 0.5};
    struct Sample {
        double x;
        double z;
        float gradient;
        float fbm;
        float ridged;
        float fbm3;
    };
    constexpr std::array<Sample, 4> kSamples = {{
        {0.3, 0.7, -0.465207815f, -0.00268229633f, 0.973833144f, 0.0525964573f},
        {-123.45, 678.9, -0.632714093f, -0.0200588275f, 0.839382648f, 0.129103094f},
        {1.0e5 + 0.25, -3.0e4 - 0.5, 0.490012705f, 0.0311451126f, 0.93662262f, 0.0574381836f},
        {-7.75, -0.125, -0.0172758028f, -0.137002751f, 0.750011563f, 0.0275242422f},
    }};
    for (const Sample& s : kSamples) {
        const float g = gradient(s.x, s.z, kFrozenSeed);
        const float f = fbm(s.x, s.z, kFrozenSeed, kFrozenFbm);
        const float r = noise::ridged2D(s.x, s.z, kFrozenSeed, kFrozenFbm, kWorld);
        const float f3 = noise::fbm3D(s.x, 0.5 * s.z, s.z, kFrozenSeed, kFrozenFbm, kWorld);
        if (g != s.gradient || f != s.fbm || r != s.ridged || f3 != s.fbm3) {
            std::printf("frozen {%.17g, %.17g, %.9gf, %.9gf, %.9gf, %.9gf}\n", s.x, s.z,
                        static_cast<double>(g), static_cast<double>(f), static_cast<double>(r),
                        static_cast<double>(f3));
        }
        TEST_CHECK(g == s.gradient && f == s.fbm && r == s.ridged && f3 == s.fbm3);
    }
}

void testGradient3D() {
    Points points(6);
    constexpr double kStep = 1.0e-4;
    bool same = true;
    bool inRange = true;
    bool continuous = true;
    int seedsDiffer = 0;
    int variesAlongY = 0;
    float peak = 0.0f;
    double sum = 0.0;
    constexpr int kCount = 4000;
    for (int i = 0; i < kCount; ++i) {
        const double x = points.next(1.0e3);
        const double y = points.next(1.0e3);
        const double z = points.next(1.0e3);
        const float g = gradient3(x, y, z);
        same = same && g == gradient3(x, y, z);
        inRange = inRange && g >= -1.0f && g <= 1.0f;
        continuous = continuous && std::abs(gradient3(x + kStep, y, z) - g) <= 1.0e-3f &&
                     std::abs(gradient3(x, y + kStep, z) - g) <= 1.0e-3f &&
                     std::abs(gradient3(x, y, z + kStep) - g) <= 1.0e-3f;
        seedsDiffer += g != gradient3(x, y, z, kSeed + 1) ? 1 : 0;
        // The noise is really three-dimensional: it changes along y as well.
        variesAlongY += g != gradient3(x, y + 0.5, z) ? 1 : 0;
        peak = std::fmax(peak, std::abs(g));
        sum += static_cast<double>(g);
    }
    TEST_CHECK(same && inRange && continuous);
    TEST_CHECK(seedsDiffer > kCount * 9 / 10 && variesAlongY > kCount * 9 / 10);
    // The noise uses a good part of its range and is centered on zero.
    TEST_CHECK(peak > 0.5f);
    TEST_CHECK(std::abs(sum / kCount) < 0.05);
}

void testGradient3DZeroOnLattice() {
    bool zero = true;
    for (int z = -5; z <= 5; ++z) {
        for (int y = -5; y <= 5; ++y) {
            for (int x = -5; x <= 5; ++x) {
                zero = zero && gradient3(x, y, z) == 0.0f;
            }
        }
    }
    TEST_CHECK(zero);
    TEST_CHECK(gradient3(-123456789.0, 987654321.0, -42.0) == 0.0f);
    TEST_CHECK(gradient3(-1.0, -1.0, -1.0, kSeed + 7) == 0.0f);
}

void testFbm3D() {
    Points points(11);
    constexpr double kStep = 1.0e-4;
    bool inRange = true;
    bool continuous = true;
    for (int i = 0; i < 2000; ++i) {
        const double x = points.next(1.0e3);
        const double y = points.next(1.0e3);
        const double z = points.next(1.0e3);
        const float f = noise::fbm3D(x, y, z, kSeed, kFbm3, kWorld);
        inRange = inRange && f >= -1.0f && f <= 1.0f;
        continuous = continuous &&
                     std::abs(noise::fbm3D(x + kStep, y, z, kSeed, kFbm3, kWorld) - f) <= 1.0e-3f &&
                     std::abs(noise::fbm3D(x, y + kStep, z, kSeed, kFbm3, kWorld) - f) <= 1.0e-3f &&
                     std::abs(noise::fbm3D(x, y, z + kStep, kSeed, kFbm3, kWorld) - f) <= 1.0e-3f;
    }
    TEST_CHECK(inRange && continuous);
    // A single octave is the gradient noise of the first octave seed, unrotated.
    const noise::FbmParams one{.octaves = 1, .frequency = 0.25};
    TEST_CHECK(noise::fbm3D(10.3, -4.6, 7.7, kSeed, one, kWorld) ==
               noise::gradient3D(10.3, -4.6, 7.7, noise::deriveSeed(kSeed, 0),
                                 noise::lattice2D(kWorld, 0.25), 0.25));
}

} // namespace

int main() {
    testDeterminism();
    testZeroOnLattice();
    testRanges();
    testContinuity();
    testSeedsDiffer();
    testRidgedSharpness();
    testNoOctaves();
    testSmoothstep();
    testFrozen();
    testGradient3D();
    testGradient3DZeroOnLattice();
    testFbm3D();
    return test::result();
}
