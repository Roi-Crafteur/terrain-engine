// FbmOctaves::ridged2DSlope: with a floor of 0, exactly the bits of ridged2D, without a memo and
// through one along a sequence of points; its head exactly the bits of a ridged noise of the
// head octaves alone, and its headFbm those of fbm2D of that noise (a noise of one octave: the
// zero lines of the valleys and their side); the gradient of a head of one octave that of the noise (finite
// differences), and that of two octaves close to it at most points; a floor of the weights that
// only raises the noise; the crease small where the head is near its crests.
#include "terrain/generation/noise_octaves.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

namespace noise = terrain::noise;
using bit_exact::Point;
using bit_exact::same;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = 4242;

noise::FbmParams ridgeParams(int octaves, noise::OctaveLattices lattices) {
    return {.octaves = octaves, .frequency = 1.0 / 24'000.0, .gain = 0.42, .lattices = lattices};
}

void testBits() {
    const WorldSize world{};
    for (const auto lattices : {noise::OctaveLattices::Rotated, noise::OctaveLattices::RotatedFast,
                                noise::OctaveLattices::Unrotated}) {
        const noise::FbmOctaves full(kSeed, ridgeParams(7, lattices), world);
        const noise::FbmOctaves head(kSeed, ridgeParams(2, lattices), world);
        noise::FbmOctaves::Memo memo;
        bool value = true;
        bool memoized = true;
        bool heads = true;
        for (const Point& p : bit_exact::randomPoints(5'000, 3e7)) {
            for (const double sharpness : {3.0, 4.0}) {
                const noise::FbmOctaves::RidgedSlope r =
                    full.ridged2DSlope(p.x, p.z, sharpness, 0.0, 2);
                const noise::FbmOctaves::RidgedSlope m =
                    full.ridged2DSlope(p.x + 3.0, p.z, sharpness, 0.0, 2, memo);
                value = value && same(r.value, full.ridged2D(p.x, p.z, sharpness));
                memoized =
                    memoized && same(m.value, full.ridged2D(p.x + 3.0, p.z, sharpness)) &&
                    same(m.head, full.ridged2DSlope(p.x + 3.0, p.z, sharpness, 0.0, 2).head);
                heads = heads && same(r.head, head.ridged2D(p.x, p.z, sharpness)) &&
                        same(r.headFbm, head.fbm2D(p.x, p.z)) &&
                        same(m.headFbm, head.fbm2D(p.x + 3.0, p.z));
            }
        }
        TEST_CHECK(value);
        TEST_CHECK(memoized);
        TEST_CHECK(heads);
    }
}

// A noise of one octave with a head of one: headFbm is fbm2D, with and without a memo, along rows.
void testHeadFbmOneOctave() {
    const WorldSize world{};
    const noise::FbmOctaves line(kSeed, {.octaves = 1, .frequency = 1.0 / 50'000.0,
                                         .lattices = noise::OctaveLattices::RotatedFast},
                                 world);
    noise::FbmOctaves::Memo memo;
    bool equal = true;
    for (const Point& p : bit_exact::randomPoints(2'000, 3e7)) {
        for (int k = 0; k < 4; ++k) {
            const double x = p.x + 2'500.0 * k;
            equal = equal && same(line.ridged2DSlope(x, p.z, 1.0, 0.0, 1, memo).headFbm,
                                  line.fbm2D(x, p.z)) &&
                    same(line.ridged2DSlope(x, p.z, 1.0, 0.0, 1).headFbm, line.fbm2D(x, p.z));
        }
    }
    TEST_CHECK(equal);
}

// The gradient of a head of one octave is that of the noise; with two, the weight of the second
// taken as constant, close to it at most points (within 35 %).
void testGradient() {
    const WorldSize world{};
    const noise::FbmOctaves full(kSeed, ridgeParams(7, noise::OctaveLattices::RotatedFast), world);
    for (const int heads : {1, 2}) {
        const noise::FbmOctaves head(kSeed, ridgeParams(heads, noise::OctaveLattices::RotatedFast),
                                     world);
        int checked = 0;
        int close = 0;
        for (const Point& p : bit_exact::randomPoints(5'000, 3e7)) {
            const noise::FbmOctaves::RidgedSlope r = full.ridged2DSlope(p.x, p.z, 4.0, 0.0, heads);
            const double h = 0.5;
            const double dx =
                (head.ridged2D(p.x + h, p.z, 4.0) - head.ridged2D(p.x - h, p.z, 4.0)) / (2.0 * h);
            const double dz =
                (head.ridged2D(p.x, p.z + h, 4.0) - head.ridged2D(p.x, p.z - h, 4.0)) / (2.0 * h);
            const double norm = std::hypot(dx, dz);
            if (!(norm > 1e-6)) {
                continue;
            }
            ++checked;
            const double error = std::hypot(r.headX - dx, r.headZ - dz) / norm;
            close += error < (heads == 1 ? 0.01 : 0.35) ? 1 : 0;
        }
        TEST_CHECK(checked > 4'000);
        std::printf("head of %d octaves: %d of %d gradients close\n", heads, close, checked);
        TEST_CHECK(close > checked * (heads == 1 ? 95 : 80) / 100);
    }
}

void testFloorAndCrease() {
    const noise::FbmOctaves full(kSeed, ridgeParams(7, noise::OctaveLattices::RotatedFast),
                                 WorldSize{});
    bool raised = true;
    bool creases = true;
    int crests = 0;
    for (const Point& p : bit_exact::randomPoints(20'000, 3e7)) {
        const noise::FbmOctaves::RidgedSlope r = full.ridged2DSlope(p.x, p.z, 4.0, 0.0, 2);
        const noise::FbmOctaves::RidgedSlope f = full.ridged2DSlope(p.x, p.z, 4.0, 0.3, 2);
        raised = raised && f.value >= r.value && f.crease == r.crease;
        creases = creases && r.crease >= 0.0f && r.crease <= 1.0f;
        if (r.head > 0.9f) {
            ++crests;
            creases = creases && r.crease < 0.1f;
        }
    }
    TEST_CHECK(raised);
    TEST_CHECK(creases);
    TEST_CHECK(crests > 0);
}

} // namespace

int main() {
    testHeadFbmOneOctave();
    testBits();
    testGradient();
    testFloorAndCrease();
    return test::result();
}
