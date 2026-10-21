// The bounds behind the shortcut of the Heightfield that skips the mask noise where the land
// factor is 0. There the mountain mask is land * max(massif, collision) with land = +0 (the
// land factor is a smoothstep, never -0), which is +0 whatever the massif as long as
// max(massif, collision) is finite and not negative, and the massif never -0 (+0 * -0 is -0).
// - massif = smoothstep(maskLow, maskHigh, fbm), with fbm a float in [-1, 1]: checked on every
//   float of [-1, 1] for the default edges, and with denormals flushed (FTZ and DAZ, where the
//   platform allows it) on every subnormal float and one float in 97 of [-1, 1] (the mode only
//   changes subnormal operands and results: the difference x - maskLow and the result are
//   normal or 0 for every x of [-1, 1] that is not itself subnormal);
// - the collision of the plates: checked on pseudo-random and edge points of the planet;
// - and the samples themselves: frozen_reference_test.
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define TERRAIN_TEST_FTZ 1
#else
#define TERRAIN_TEST_FTZ 0
#endif

namespace {

// A massif or a collision the shortcut accepts: finite, not negative, and not -0.
bool bounded(float value) {
    return std::isfinite(value) && value >= 0.0f && !std::signbit(value);
}

// The floats of [-1, 1] whose magnitude has bits = 0, step, 2 step... (step 1: every one)
// through smoothstep(low, high, x), with every subnormal one: the count of results out of
// bounds.
std::uint64_t massifsOutOfBounds(float low, float high, std::uint32_t step) {
    constexpr std::uint32_t kOne = 0x3F800000U;        // bits of 1.0f
    constexpr std::uint32_t kSubnormals = 0x00800000U; // bits of the smallest normal float
    std::uint64_t out = 0;
    for (const std::uint32_t sign : {0x00000000U, 0x80000000U}) {
        const auto check = [&out, sign, low, high](std::uint32_t bits) {
            const float x = std::bit_cast<float>(sign | bits);
            out += bounded(terrain::noise::smoothstep(low, high, x)) ? 0U : 1U;
        };
        for (std::uint32_t bits = 0; bits < kSubnormals; ++bits) {
            check(bits);
        }
        for (std::uint32_t bits = kSubnormals; bits <= kOne - step; bits += step) {
            check(bits);
        }
        check(kOne);
    }
    return out;
}

void checkMassif() {
    const terrain::HeightfieldParams params;
    const std::uint64_t out = massifsOutOfBounds(params.maskLow, params.maskHigh, 1);
    std::printf("massif on every float of [-1, 1]: %llu out of bounds\n",
                static_cast<unsigned long long>(out));
    TEST_CHECK(out == 0);
#if TERRAIN_TEST_FTZ
    const unsigned int ftz = _MM_GET_FLUSH_ZERO_MODE();
    const unsigned int daz = _MM_GET_DENORMALS_ZERO_MODE();
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    const std::uint64_t flushed = massifsOutOfBounds(params.maskLow, params.maskHigh, 97);
    _MM_SET_FLUSH_ZERO_MODE(ftz);
    _MM_SET_DENORMALS_ZERO_MODE(daz);
    std::printf("massif with FTZ and DAZ (subnormals, 1 float in 97): %llu out of bounds\n",
                static_cast<unsigned long long>(flushed));
    TEST_CHECK(flushed == 0);
#endif
}

void checkCollision() {
    terrain::HeightfieldParams params;
    params.macroContinent = 0.25f;
    params.plates.amplitude = 0.5f;
    const terrain::Heightfield planet(20261019, params);
    std::vector<bit_exact::Point> points = bit_exact::randomPoints(100'000, 4e7);
    const std::vector<bit_exact::Point> edges = bit_exact::edgePoints(planet.world());
    points.insert(points.end(), edges.begin(), edges.end());
    std::size_t out = 0;
    std::size_t colliding = 0;
    for (const bit_exact::Point& p : points) {
        const float collision = planet.sampleAt(p.x, p.z).plateCollision;
        out += bounded(collision) ? 0U : 1U;
        colliding += collision > 0.0f ? 1U : 0U;
    }
    std::printf("plate collision on %zu points (%zu in a belt): %zu out of bounds\n",
                points.size(), colliding, out);
    TEST_CHECK(out == 0);
    TEST_CHECK(colliding > 0);
}

} // namespace

int main() {
    checkMassif();
    checkCollision();
    return test::result();
}
