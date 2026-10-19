#include "terrain/generation/noise.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstdint>

namespace {

namespace noise = terrain::noise;

constexpr std::uint64_t kSeed = 42;
constexpr noise::FbmParams kFbm{.octaves = 5, .frequency = 0.37};

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

void testDeterminism() {
    Points points(1);
    for (int i = 0; i < 1000; ++i) {
        const double x = points.next(1.0e5);
        const double z = points.next(1.0e5);
        TEST_CHECK(noise::gradient2D(x, z, kSeed) == noise::gradient2D(x, z, kSeed));
        TEST_CHECK(noise::fbm2D(x, z, kSeed, kFbm) == noise::fbm2D(x, z, kSeed, kFbm));
        TEST_CHECK(noise::ridged2D(x, z, kSeed, kFbm) == noise::ridged2D(x, z, kSeed, kFbm));
    }
    TEST_CHECK(noise::deriveSeed(kSeed, 3) == noise::deriveSeed(kSeed, 3));
    TEST_CHECK(noise::deriveSeed(kSeed, 3) != noise::deriveSeed(kSeed, 4));
    TEST_CHECK(noise::deriveSeed(kSeed, 3) != noise::deriveSeed(kSeed + 1, 3));
}

void testZeroOnLattice() {
    for (int z = -20; z <= 20; ++z) {
        for (int x = -20; x <= 20; ++x) {
            TEST_CHECK(noise::gradient2D(x, z, kSeed) == 0.0f);
        }
    }
    TEST_CHECK(noise::gradient2D(-123456789.0, 987654321.0, kSeed) == 0.0f);
}

void testRanges() {
    Points points(2);
    float low = 0.0f;
    float high = 0.0f;
    bool inRange = true;
    for (int i = 0; i < 100000; ++i) {
        const double x = points.next(1.0e4);
        const double z = points.next(1.0e4);
        const float g = noise::gradient2D(x, z, kSeed);
        const float f = noise::fbm2D(x, z, kSeed, kFbm);
        const float r = noise::ridged2D(x, z, kSeed, kFbm);
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
        const float g = noise::gradient2D(x, z, kSeed);
        const float f = noise::fbm2D(x, z, kSeed, kFbm);
        const float r = noise::ridged2D(x, z, kSeed, kFbm);
        // Slopes stay below a few units per lattice cell, octave frequencies included.
        continuous = continuous &&
                     std::abs(noise::gradient2D(x + kStep, z, kSeed) - g) < 1.0e-3f &&
                     std::abs(noise::gradient2D(x, z + kStep, kSeed) - g) < 1.0e-3f &&
                     std::abs(noise::fbm2D(x + kStep, z, kSeed, kFbm) - f) < 1.0e-2f &&
                     std::abs(noise::ridged2D(x, z + kStep, kSeed, kFbm) - r) < 1.0e-2f;
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
        if (noise::gradient2D(x, z, kSeed) != noise::gradient2D(x, z, kSeed + 1)) {
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
        lower = lower &&
                noise::ridged2D(x, z, kSeed, kFbm, 4.0) <= noise::ridged2D(x, z, kSeed, kFbm, 2.0);
    }
    TEST_CHECK(lower);
}

void testNoOctaves() {
    const noise::FbmParams none{.octaves = 0};
    TEST_CHECK(noise::fbm2D(0.3, 0.7, kSeed, none) == 0.0f);
    TEST_CHECK(noise::ridged2D(0.3, 0.7, kSeed, none) == 0.0f);
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
    return test::result();
}
