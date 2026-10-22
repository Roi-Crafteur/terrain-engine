// Detail slopes of the far horizon: each texel is the Sobel filter of heightAt one spacing apart
// at its canonical position, bit for bit (on several levels, at the corner of the world); the
// same bits whatever the rectangle and the image; the texel (0, 0) of a sample lies on it; the
// detail keeps the relief the grid of a coarse level erases; and the invalid arguments.
#include "terrain/streaming/horizon_normals.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

using terrain::DetailSlope;
using terrain::HorizonNormalSampler;
using terrain::HorizonParams;
using terrain::HorizonRect;

constexpr int kRatio = 4;

// The texel at fine index (t, u) of a level, from heightAt.
DetailSlope oracle(const terrain::Heightfield& field, const HorizonParams& params, int level,
                   std::int64_t t, std::int64_t u) {
    const int spacing = terrain::levelPitch(params, level) / kRatio;
    const terrain::HorizonIndex counts = terrain::levelCounts(params, level);
    const auto h = [&](std::int64_t i, std::int64_t k) {
        const double x = terrain::samplePosition(terrain::wrapIndex(t + i, counts.x * kRatio), spacing);
        const double z = terrain::samplePosition(terrain::wrapIndex(u + k, counts.z * kRatio), spacing);
        return static_cast<double>(field.heightAt(x, z));
    };
    const double east = h(1, -1) + 2.0 * h(1, 0) + h(1, 1);
    const double west = h(-1, -1) + 2.0 * h(-1, 0) + h(-1, 1);
    const double south = h(-1, 1) + 2.0 * h(0, 1) + h(1, 1);
    const double north = h(-1, -1) + 2.0 * h(0, -1) + h(1, -1);
    const double run = 8.0 * spacing;
    return {static_cast<float>((east - west) / run), static_cast<float>((south - north) / run)};
}

HorizonRect around(const HorizonParams& params, int level, double x, double z, int half) {
    const int pitch = terrain::levelPitch(params, level);
    const auto i = static_cast<std::int64_t>(std::floor(x / pitch));
    const auto k = static_cast<std::int64_t>(std::floor(z / pitch));
    return {i - half, k - half, i + half, k + half};
}

void testOracle(const horizon_test::World& world) {
    const HorizonParams params;
    const HorizonNormalSampler sampler(world.field, params, kRatio);
    const double corner = params.world.x / 2.0;
    for (const int level : {0, 3, 6}) {
        for (const auto& [x, z] : {std::pair{horizon_test::kX, horizon_test::kZ},
                                   std::pair{corner, params.world.z / 2.0}}) {
            const HorizonRect rect = around(params, level, x, z, 2);
            const std::vector<DetailSlope> slopes = sampler.sample(level, rect);
            const std::int64_t width = rect.width() * kRatio;
            bool same = true;
            for (std::int64_t v = 0; v < rect.height() * kRatio; v += 3) {
                for (std::int64_t w = 0; w < width; w += 3) {
                    const DetailSlope expected =
                        oracle(world.field, params, level, rect.x0 * kRatio + w, rect.z0 * kRatio + v);
                    same = same && slopes[static_cast<std::size_t>(v * width + w)] == expected;
                }
            }
            TEST_CHECK(same);
        }
    }
}

// A rectangle inside another one, and the same samples a whole world away: the same bits.
void testIndependence(const horizon_test::World& world) {
    const HorizonParams params;
    const HorizonNormalSampler sampler(world.field, params, kRatio);
    const int level = 2;
    const HorizonRect big = around(params, level, horizon_test::kX, horizon_test::kZ, 6);
    const HorizonRect small{big.x0 + 3, big.z0 + 5, big.x0 + 7, big.z0 + 6};
    const std::vector<DetailSlope> a = sampler.sample(level, big);
    const std::vector<DetailSlope> b = sampler.sample(level, small);
    const terrain::HorizonIndex counts = terrain::levelCounts(params, level);
    const HorizonRect moved{small.x0 + counts.x, small.z0 - counts.z, small.x1 + counts.x,
                            small.z1 - counts.z};
    const std::vector<DetailSlope> c = sampler.sample(level, moved);
    bool same = b == c;
    const std::int64_t bigWidth = big.width() * kRatio;
    const std::int64_t smallWidth = small.width() * kRatio;
    for (std::int64_t v = 0; v < small.height() * kRatio; ++v) {
        for (std::int64_t w = 0; w < smallWidth; ++w) {
            const std::int64_t bv = (small.z0 - big.z0) * kRatio + v;
            const std::int64_t bw = (small.x0 - big.x0) * kRatio + w;
            same = same && b[static_cast<std::size_t>(v * smallWidth + w)] ==
                               a[static_cast<std::size_t>(bv * bigWidth + bw)];
        }
    }
    TEST_CHECK(same);
}

// The spread of the slopes over a mountain: the detail of a coarse level keeps much more of it
// than the gradients of its samples (central differences two pitches wide).
void testDetail(const horizon_test::World& world) {
    const HorizonParams params;
    const HorizonNormalSampler normals(world.field, params, kRatio);
    const terrain::HorizonSampler samples(world.field, world.ecology, params);
    const int level = 3;
    const HorizonRect rect = around(params, level, -1'441'792.0, -3'932'160.0, 8);
    double coarse = 0.0;
    for (const terrain::HorizonCell& cell : samples.sample(level, rect)) {
        coarse += static_cast<double>(cell.gradX) * cell.gradX + static_cast<double>(cell.gradZ) * cell.gradZ;
    }
    coarse /= static_cast<double>(rect.count());
    double detail = 0.0;
    const std::vector<DetailSlope> slopes = normals.sample(level, rect);
    for (const DetailSlope& slope : slopes) {
        detail += static_cast<double>(slope.gradX) * slope.gradX + static_cast<double>(slope.gradZ) * slope.gradZ;
    }
    detail /= static_cast<double>(slopes.size());
    std::printf("mean squared slope over a mountain, level 3: grid %.3f, detail %.3f\n", coarse,
                detail);
    TEST_CHECK(detail > 1.2 * coarse);
}

template <typename F>
bool throwsInvalid(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void testInvalid(const horizon_test::World& world) {
    const HorizonParams params;
    for (const int ratio : {0, 3, 32, -4}) {
        TEST_CHECK(throwsInvalid([&] { HorizonNormalSampler(world.field, params, ratio); }));
    }
    const terrain::Heightfield other(horizon_test::kSeed, {}, terrain::WorldSize{65'536.0 * 64, 65'536.0 * 32});
    TEST_CHECK(throwsInvalid([&] { HorizonNormalSampler(other, params, kRatio); }));
    const HorizonNormalSampler sampler(world.field, params, kRatio);
    const HorizonRect rect{0, 0, 2, 2};
    std::vector<DetailSlope> out(static_cast<std::size_t>(sampler.texelCount(rect)) - 1);
    TEST_CHECK(throwsInvalid([&] { sampler.fill(0, rect, out); }));
    TEST_CHECK(throwsInvalid([&] { (void)sampler.sample(params.levels, rect); }));
    TEST_CHECK(sampler.spacing(0) == 2 && sampler.spacing(3) == 16);
    TEST_CHECK(sampler.sample(0, HorizonRect{}).empty());
}

} // namespace

int main() {
    const horizon_test::World world;
    testOracle(world);
    testIndependence(world);
    testDetail(world);
    testInvalid(world);
    return test::result();
}
