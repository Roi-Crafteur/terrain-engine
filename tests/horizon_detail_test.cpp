// The detail of the far horizon: the slopes, the point heights and the lifts of
// HorizonDetailSampler are those of HorizonNormalSampler::sample, Heightfield::sampleAt and HorizonCrestSampler::fill
// (its points from sampleAt, as the epochs give them), bit for bit, for normal and crest ratios
// sharing one grid or not, on levels 0 to 9, in the mountains of the default world and across the
// corner of the wrapped world; the same bits for a rectangle and its parts; invalid parameters.
#include "terrain/streaming/horizon_detail.hpp"

#include "horizon_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using terrain::HorizonDetail;
using terrain::HorizonDetailSampler;
using terrain::HorizonIndex;
using terrain::HorizonParams;
using terrain::HorizonRect;

HorizonRect around(const HorizonParams& params, int level, double x, double z) {
    const HorizonIndex c = terrain::canonicalCenter(params, level, x, z);
    return {c.x - 2, c.z - 1, c.x + 2, c.z + 2}; // 4 x 3 samples
}

// The point heights of a rectangle grown by one sample, by sampleAt (as the epochs read them).
std::vector<float> margined(const terrain::Heightfield& field, const HorizonParams& params,
                            int level, const HorizonRect& rect) {
    const int pitch = terrain::levelPitch(params, level);
    const HorizonIndex counts = terrain::levelCounts(params, level);
    std::vector<float> heights;
    for (std::int64_t k = rect.z0 - 1; k <= rect.z1; ++k) {
        for (std::int64_t i = rect.x0 - 1; i <= rect.x1; ++i) {
            heights.push_back(
                field
                    .sampleAt(terrain::samplePosition(terrain::wrapIndex(i, counts.x), pitch),
                              terrain::samplePosition(terrain::wrapIndex(k, counts.z), pitch))
                    .height);
        }
    }
    return heights;
}

// The detail of a rectangle against each sampler alone; returns the samples that rise.
int compare(const terrain::Heightfield& field, const HorizonParams& params, int normalRatio,
            int level, const HorizonRect& rect) {
    const HorizonDetailSampler sampler(field, params, normalRatio);
    const HorizonDetail detail = sampler.sample(level, rect);
    if (normalRatio > 0) {
        const terrain::HorizonNormalSampler normals(field, params, normalRatio);
        TEST_CHECK(detail.slopes == normals.sample(level, rect));
    } else {
        TEST_CHECK(detail.slopes.empty());
    }
    const terrain::HorizonCrestSampler crests(field, params);
    if (!crests.raises(level)) {
        TEST_CHECK(detail.lifts.empty() && detail.points.empty());
        return 0;
    }
    const std::vector<float> ground = margined(field, params, level, rect);
    std::vector<float> lifts(static_cast<std::size_t>(rect.count()));
    crests.fill(level, rect, ground, lifts);
    TEST_CHECK(detail.lifts == lifts);
    int raised = 0;
    std::size_t n = 0;
    for (std::int64_t k = 0; k < rect.height(); ++k) {
        for (std::int64_t i = 0; i < rect.width(); ++i, ++n) {
            const auto at = static_cast<std::size_t>((k + 1) * (rect.width() + 2) + i + 1);
            TEST_CHECK(detail.points[n] == ground[at]);
            raised += lifts[n] > 0.0f ? 1 : 0;
        }
    }
    return raised;
}

void testRatios(const horizon_test::World& world) {
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(world.field);
    const terrain::WorldSize size;
    const std::pair<double, double> places[] = {
        {mountain.x * terrain::kChunkSizeMeters, mountain.z * terrain::kChunkSizeMeters},
        {size.x / 2.0, size.z / 2.0}};
    constexpr std::pair<int, int> kPairs[] = {{4, 4}, {4, 2}, {2, 4}, {8, 2},
                                              {0, 4}, {4, 0}, {4, 16}};
    int raised = 0;
    int sharing = 0;
    for (const auto& [normalRatio, crestRatio] : kPairs) {
        HorizonParams params;
        params.crestRatio = crestRatio;
        const HorizonDetailSampler sampler(world.field, params, normalRatio);
        for (const int level : {0, 1, 2, 5, 9}) {
            sharing += sampler.shared(level) ? 1 : 0;
            const bool crests = level > 0 && crestRatio > 0;
            TEST_CHECK(sampler.shared(level) ==
                       (crests && normalRatio > 0 && crestRatio <= normalRatio));
            TEST_CHECK(sampler.hasDetail(level) == (normalRatio > 0 || crests));
            for (const auto& [x, z] : places) {
                raised += compare(world.field, params, normalRatio, level,
                                  around(params, level, x, z));
            }
        }
    }
    std::printf("detail: %d samples rise, %d levels share one grid\n", raised, sharing);
    TEST_CHECK(raised > 0 && sharing == 12);
}

// A rectangle and its two halves, and an empty one.
void testParts(const horizon_test::World& world) {
    const HorizonParams params;
    const HorizonDetailSampler sampler(world.field, params, 4);
    const HorizonRect rect = around(params, 3, horizon_test::kX, horizon_test::kZ);
    const HorizonDetail whole = sampler.sample(3, rect);
    const HorizonDetail top = sampler.sample(3, {rect.x0, rect.z0, rect.x1, rect.z0 + 1});
    const HorizonDetail rest = sampler.sample(3, {rect.x0, rect.z0 + 1, rect.x1, rect.z1});
    std::vector<float> lifts = top.lifts;
    lifts.insert(lifts.end(), rest.lifts.begin(), rest.lifts.end());
    std::vector<terrain::DetailSlope> slopes = top.slopes;
    slopes.insert(slopes.end(), rest.slopes.begin(), rest.slopes.end());
    TEST_CHECK(lifts == whole.lifts && slopes == whole.slopes);
    const HorizonDetail none = sampler.sample(3, {});
    TEST_CHECK(none.slopes.empty() && none.lifts.empty() && none.points.empty());
}

template <typename F>
bool throws(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void testInvalid(const horizon_test::World& world) {
    const HorizonParams params;
    TEST_CHECK(throws([&] { HorizonDetailSampler(world.field, params, 3); }));
    TEST_CHECK(throws([&] { HorizonDetailSampler(world.field, params, 16); })); // > 8 m
    HorizonParams bad;
    bad.crestRatio = 3;
    TEST_CHECK(throws([&] { HorizonDetailSampler(world.field, bad, 4); }));
    const HorizonDetailSampler sampler(world.field, params, 0);
    TEST_CHECK(sampler.normals() == nullptr && sampler.normalRatio() == 0);
    TEST_CHECK(throws([&] { (void)sampler.sample(10, {0, 0, 1, 1}); }));
    TEST_CHECK(throws([&] { (void)sampler.sample(-1, {0, 0, 1, 1}); }));
}

} // namespace

int main() {
    const horizon_test::World world;
    testRatios(world);
    testParts(world);
    testInvalid(world);
    return test::result();
}
