#include "terrain/generation/noise.hpp"

#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace {

namespace noise = terrain::noise;

constexpr std::uint64_t kSeed = 42;
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

// Frozen outputs of the 2D noise (printed with %.9g, exact for a float): adding the 3D noise
// must not change a single bit of the 2D functions or of their helpers.
void testFrozen2D() {
    constexpr std::uint64_t kFrozenSeed = 20261019;
    constexpr noise::FbmParams kFrozenFbm{
        .octaves = 4, .frequency = 0.01, .lacunarity = 2.0, .gain = 0.5};
    struct Sample {
        double x;
        double z;
        float gradient;
        float fbm;
        float ridged;
    };
    constexpr std::array<Sample, 4> kSamples = {{
        {0.3, 0.7, -0.465207815f, 0.00549232913f, 0.975529909f},
        {-123.45, 678.9, -0.245905474f, 0.326726794f, 0.329790741f},
        {1.0e5 + 0.25, -3.0e4 - 0.5, 0.162137732f, 0.0358453542f, 0.928310394f},
        {-7.75, -0.125, -0.0830461159f, -0.120558046f, 0.775927007f},
    }};
    for (const Sample& s : kSamples) {
        TEST_CHECK(noise::gradient2D(s.x, s.z, kFrozenSeed) == s.gradient);
        TEST_CHECK(noise::fbm2D(s.x, s.z, kFrozenSeed, kFrozenFbm) == s.fbm);
        TEST_CHECK(noise::ridged2D(s.x, s.z, kFrozenSeed, kFrozenFbm) == s.ridged);
    }
}

void testGradient3DDeterminism() {
    Points points(6);
    bool same = true;
    for (int i = 0; i < 1000; ++i) {
        const double x = points.next(1.0e5);
        const double y = points.next(1.0e5);
        const double z = points.next(1.0e5);
        same = same && noise::gradient3D(x, y, z, kSeed) == noise::gradient3D(x, y, z, kSeed);
    }
    TEST_CHECK(same);
}

void testGradient3DSeedsDiffer() {
    Points points(7);
    int different = 0;
    constexpr int kCount = 1000;
    for (int i = 0; i < kCount; ++i) {
        const double x = points.next(1.0e3);
        const double y = points.next(1.0e3);
        const double z = points.next(1.0e3);
        if (noise::gradient3D(x, y, z, kSeed) != noise::gradient3D(x, y, z, kSeed + 1)) {
            ++different;
        }
    }
    TEST_CHECK(different > kCount * 9 / 10);
}

void testGradient3DZeroOnLattice() {
    bool zero = true;
    for (int z = -5; z <= 5; ++z) {
        for (int y = -5; y <= 5; ++y) {
            for (int x = -5; x <= 5; ++x) {
                zero = zero && noise::gradient3D(x, y, z, kSeed) == 0.0f;
            }
        }
    }
    TEST_CHECK(zero);
    TEST_CHECK(noise::gradient3D(-123456789.0, 987654321.0, -42.0, kSeed) == 0.0f);
    TEST_CHECK(noise::gradient3D(-1.0, -1.0, -1.0, kSeed + 7) == 0.0f);
}

void testGradient3DRange() {
    Points points(8);
    float peak = 0.0f;
    double sum = 0.0;
    bool inRange = true;
    constexpr int kCount = 20000;
    for (int i = 0; i < kCount; ++i) {
        const double x = points.next(1.0e3);
        const double y = points.next(1.0e3);
        const double z = points.next(1.0e3);
        const float g = noise::gradient3D(x, y, z, kSeed);
        inRange = inRange && g >= -1.0f && g <= 1.0f;
        peak = std::fmax(peak, std::abs(g));
        sum += static_cast<double>(g);
    }
    TEST_CHECK(inRange);
    // The noise uses a good part of its range and is centered on zero.
    TEST_CHECK(peak > 0.5f);
    TEST_CHECK(std::abs(sum / kCount) < 0.05);
}

void testGradient3DContinuity() {
    Points points(9);
    constexpr double kStep = 1.0e-4;
    bool continuous = true;
    for (int i = 0; i < 2000; ++i) {
        const double x = points.next(1.0e3);
        const double y = points.next(1.0e3);
        const double z = points.next(1.0e3);
        const float g = noise::gradient3D(x, y, z, kSeed);
        continuous = continuous &&
                     std::abs(noise::gradient3D(x + kStep, y, z, kSeed) - g) <= 1.0e-3f &&
                     std::abs(noise::gradient3D(x, y + kStep, z, kSeed) - g) <= 1.0e-3f &&
                     std::abs(noise::gradient3D(x, y, z + kStep, kSeed) - g) <= 1.0e-3f;
    }
    TEST_CHECK(continuous);
}

// The noise is really three-dimensional: it changes along y as well, not only in x and z.
void testGradient3DVariesAlongY() {
    Points points(10);
    int different = 0;
    constexpr int kCount = 1000;
    for (int i = 0; i < kCount; ++i) {
        const double x = points.next(1.0e3);
        const double y = points.next(1.0e3);
        const double z = points.next(1.0e3);
        if (noise::gradient3D(x, y, z, kSeed) != noise::gradient3D(x, y + 0.5, z, kSeed)) {
            ++different;
        }
    }
    TEST_CHECK(different > kCount * 9 / 10);
}

void testFbm3D() {
    Points points(11);
    constexpr double kStep = 1.0e-4;
    bool same = true;
    bool inRange = true;
    bool continuous = true;
    for (int i = 0; i < 2000; ++i) {
        const double x = points.next(1.0e3);
        const double y = points.next(1.0e3);
        const double z = points.next(1.0e3);
        const float f = noise::fbm3D(x, y, z, kSeed, kFbm3);
        same = same && f == noise::fbm3D(x, y, z, kSeed, kFbm3);
        inRange = inRange && f >= -1.0f && f <= 1.0f;
        continuous = continuous &&
                     std::abs(noise::fbm3D(x + kStep, y, z, kSeed, kFbm3) - f) <= 1.0e-3f &&
                     std::abs(noise::fbm3D(x, y + kStep, z, kSeed, kFbm3) - f) <= 1.0e-3f &&
                     std::abs(noise::fbm3D(x, y, z + kStep, kSeed, kFbm3) - f) <= 1.0e-3f;
    }
    TEST_CHECK(same);
    TEST_CHECK(inRange);
    TEST_CHECK(continuous);
}

void testFbm3DOctaves() {
    const noise::FbmParams none{.octaves = 0};
    TEST_CHECK(noise::fbm3D(0.3, 0.5, 0.7, kSeed, none) == 0.0f);
    // A single octave is the gradient noise of the first octave seed, at the scaled position.
    const noise::FbmParams one{.octaves = 1, .frequency = 0.25};
    TEST_CHECK(noise::fbm3D(10.3, -4.6, 7.7, kSeed, one) ==
               noise::gradient3D(2.575, -1.15, 1.925, noise::deriveSeed(kSeed, 0)));
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
    testFrozen2D();
    testGradient3DDeterminism();
    testGradient3DSeedsDiffer();
    testGradient3DZeroOnLattice();
    testGradient3DRange();
    testGradient3DContinuity();
    testGradient3DVariesAlongY();
    testFbm3D();
    testFbm3DOctaves();
    return test::result();
}
