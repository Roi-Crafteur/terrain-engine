// Crest prefilter of the far horizon: the lifts are those of a brute-force oracle on heightAt bit
// for bit, in the mountains of the default world and of the legacy relief, on levels 1 to 9 and
// for several ratios; never negative, never above the top of the cells, none on the valley
// floors, on level 0 or under the sea; the same bits whatever the rectangle, across the edges
// and the corner of the wrapped world; with crestRatio 0 the samples are the point heights, and
// with crests only the heights change (gradients, materials, biomes, flags and edits as
// before); invalid parameters.
#include "terrain/streaming/horizon_crests.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include "horizon_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using terrain::HorizonCell;
using terrain::HorizonCrestSampler;
using terrain::HorizonIndex;
using terrain::HorizonParams;
using terrain::HorizonRect;

// The ground of the fine lattice of a level, and the top of a cell, by heightAt.
struct Oracle {
    const terrain::Heightfield& field;
    HorizonParams params;
    int level = 1;

    [[nodiscard]] int ratio() const { return params.crestRatio; }
    [[nodiscard]] float fine(std::int64_t u, std::int64_t v) const {
        const int spacing = terrain::levelPitch(params, level) / ratio();
        const HorizonIndex counts = terrain::levelCounts(params, level);
        return field.heightAt(
            terrain::samplePosition(terrain::wrapIndex(u, counts.x * ratio()), spacing),
            terrain::samplePosition(terrain::wrapIndex(v, counts.z * ratio()), spacing));
    }
    [[nodiscard]] float point(std::int64_t i, std::int64_t k) const {
        return fine(i * ratio(), k * ratio());
    }
    // Highest node on the sides and at the center of the cell from sample (i, k) to
    // (i + 1, k + 1).
    [[nodiscard]] float top(std::int64_t i, std::int64_t k) const {
        const std::int64_t u = i * ratio();
        const std::int64_t v = k * ratio();
        float best = fine(u + ratio() / 2, v + ratio() / 2);
        for (std::int64_t t = 0; t <= ratio(); ++t) {
            best = std::max({best, fine(u + t, v), fine(u + t, v + ratio()), fine(u, v + t),
                             fine(u + ratio(), v + t)});
        }
        return best;
    }
    // The lift of sample (i, k), as horizon_crests.hpp states it.
    [[nodiscard]] float lift(std::int64_t i, std::int64_t k) const {
        const float own = point(i, k);
        if (level == 0 || own < terrain::kSeaLevel) {
            return 0.0f;
        }
        double best = 0.0;
        for (const std::int64_t ci : {i - 1, i}) {
            for (const std::int64_t ck : {k - 1, k}) {
                const float high = std::max(std::max(point(ci, ck), point(ci + 1, ck)),
                                            std::max(point(ci, ck + 1), point(ci + 1, ck + 1)));
                if (own == high) { // the highest corner of the cell
                    best = std::max(best, static_cast<double>(top(ci, ck)) - high);
                }
            }
        }
        return static_cast<float>(best);
    }
};

HorizonRect around(const HorizonParams& params, int level, double x, double z, int half) {
    const HorizonIndex c = terrain::canonicalCenter(params, level, x, z);
    return {c.x - half, c.z - half, c.x + half, c.z + half};
}

// How many samples of a rectangle rise, and how many are valley floors.
struct Counts {
    int raised = 0;
    int floors = 0;
};

// The lifts of a rectangle against the oracle, bit for bit.
Counts compareWithOracle(const terrain::Heightfield& field, const HorizonParams& params, int level,
                      const HorizonRect& rect) {
    const HorizonCrestSampler sampler(field, params);
    const std::vector<float> lifts = sampler.sample(level, rect);
    const Oracle oracle{field, params, level};
    Counts counts;
    std::size_t n = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
            const float expected = oracle.lift(i, k);
            TEST_CHECK(lifts[n] == expected);
            counts.raised += lifts[n] > 0.0f ? 1 : 0;
            // Never negative, never above the top of the 4 cells around the sample.
            float top = oracle.top(i - 1, k - 1);
            for (const auto& [di, dk] : {std::pair{0, -1}, std::pair{-1, 0}, std::pair{0, 0}}) {
                top = std::max(top, oracle.top(i + di, k + dk));
            }
            TEST_CHECK(lifts[n] >= 0.0f);
            TEST_CHECK(static_cast<double>(oracle.point(i, k)) + lifts[n] <= top + 1e-3);
            // A valley floor (lower than both neighbors along an axis) never rises.
            const float p = oracle.point(i, k);
            const bool floorX = p < oracle.point(i - 1, k) && p < oracle.point(i + 1, k);
            const bool floorZ = p < oracle.point(i, k - 1) && p < oracle.point(i, k + 1);
            TEST_CHECK(!(floorX || floorZ) || lifts[n] == 0.0f);
            counts.floors += floorX || floorZ ? 1 : 0;
        }
    }
    return counts;
}

void testOracle() {
    const horizon_test::World world;
    const terrain::Heightfield legacy(horizon_test::kSeed, test_relief::legacyRelief());
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(world.field);
    const double x = mountain.x * terrain::kChunkSizeMeters + 4.0;
    const double z = mountain.z * terrain::kChunkSizeMeters + 4.0;
    int floors = 0;
    for (const int ratio : {2, 4, 8}) {
        HorizonParams params;
        params.crestRatio = ratio;
        for (const int level : {1, 3, 6, 9}) {
            const Counts a = compareWithOracle(world.field, params, level,
                                               around(params, level, x, z, 4));
            const HorizonRect legacyRect =
                around(params, level, horizon_test::kX, horizon_test::kZ, 4);
            const Counts b = compareWithOracle(legacy, params, level, legacyRect);
            std::printf("ratio %d, level %d: %d and %d of 64 samples rise, %d and %d valley "
                        "floors\n",
                        ratio, level, a.raised, b.raised, a.floors, b.floors);
            // The mountains of the default world have crests on every level; the legacy ones
            // are smooth at 16 m there.
            TEST_CHECK(a.raised > 0);
            TEST_CHECK(level == 1 || b.raised > 0);
            floors += a.floors + b.floors;
        }
    }
    TEST_CHECK(floors > 100); // the valley floors were tested
}

// Level 0 and the samples under the sea never rise; the deep ocean at the corner of the world
// has no lift at all.
void testLevelZeroAndSea() {
    const horizon_test::World world;
    const HorizonParams params;
    const HorizonCrestSampler sampler(world.field, params);
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(world.field);
    const double x = mountain.x * terrain::kChunkSizeMeters;
    const double z = mountain.z * terrain::kChunkSizeMeters;
    TEST_CHECK(!sampler.raises(0) && sampler.raises(1));
    for (const float lift : sampler.sample(0, around(params, 0, x, z, 8))) {
        TEST_CHECK(lift == 0.0f);
    }
    const Oracle oracle{world.field, params, 4};
    int sea = 0;
    for (const auto& [px, pz] : {std::pair{x, z}, std::pair{params.world.x / 2.0, 0.0},
                                 std::pair{params.world.x / 2.0, params.world.z / 2.0}}) {
        const HorizonRect rect = around(params, 4, px, pz, 8);
        const std::vector<float> lifts = sampler.sample(4, rect);
        std::size_t n = 0;
        for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
            for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
                if (oracle.point(i, k) < terrain::kSeaLevel) {
                    TEST_CHECK(lifts[n] == 0.0f);
                    ++sea;
                }
            }
        }
    }
    TEST_CHECK(sea > 0);
}

// The lifts of a rectangle are those of its parts and of single samples.
void testRectangles() {
    const horizon_test::World world;
    const HorizonParams params;
    const HorizonCrestSampler sampler(world.field, params);
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(world.field);
    const double x = mountain.x * terrain::kChunkSizeMeters;
    const double z = mountain.z * terrain::kChunkSizeMeters;
    for (const int level : {2, 5}) {
        const HorizonRect rect = around(params, level, x, z, 6);
        const std::vector<float> whole = sampler.sample(level, rect);
        const HorizonRect top{rect.x0, rect.z0, rect.x1, rect.z0 + 5};
        const HorizonRect bottom{rect.x0, rect.z0 + 5, rect.x1, rect.z1};
        std::vector<float> parts = sampler.sample(level, top);
        const std::vector<float> rest = sampler.sample(level, bottom);
        parts.insert(parts.end(), rest.begin(), rest.end());
        TEST_CHECK(parts == whole);
        const HorizonRect one{rect.x0 + 7, rect.z0 + 3, rect.x0 + 8, rect.z0 + 4};
        TEST_CHECK(sampler.sample(level, one).front() ==
                   whole[static_cast<std::size_t>(3 * rect.width() + 7)]);
    }
}

// The first row of 64 km whose ground rises above 500 m at the east edge of the world, or 0.
double landAtTheEdge(const terrain::Heightfield& field, const terrain::WorldSize& world) {
    for (double z = -world.z / 2.0; z < world.z / 2.0; z += 65'536.0) {
        if (field.heightAt(world.x / 2.0, z) > 500.0f) {
            return z;
        }
    }
    return 0.0;
}

// Across the east edge (on land) and at the corner of the world: every sample has the lift of
// its canonical image, computed alone.
void testWorldEdges() {
    const horizon_test::World world;
    const HorizonParams params;
    const HorizonCrestSampler sampler(world.field, params);
    const double row = landAtTheEdge(world.field, params.world);
    int raised = 0;
    for (const int level : {2, 5, 9}) {
        const int pitch = terrain::levelPitch(params, level);
        const HorizonIndex counts = terrain::levelCounts(params, level);
        const auto k0 = static_cast<std::int64_t>(row / pitch);
        for (const std::int64_t k : {k0, counts.z / 2}) {
            const HorizonRect rect{counts.x / 2 - 4, k - 4, counts.x / 2 + 4, k + 4};
            const std::vector<float> lifts = sampler.sample(level, rect);
            std::size_t n = 0;
            for (std::int64_t b = rect.z0; b < rect.z1; ++b) {
                for (std::int64_t a = rect.x0; a < rect.x1; ++a, ++n) {
                    const HorizonIndex c = terrain::canonicalSample({a, b}, counts);
                    TEST_CHECK(sampler.sample(level, {c.x, c.z, c.x + 1, c.z + 1}).front() ==
                               lifts[n]);
                    raised += lifts[n] > 0.0f ? 1 : 0;
                }
            }
        }
    }
    std::printf("world edges: land row at z = %.0f m, %d samples rise\n", row, raised);
}

// With crestRatio 0 the samples are the point heights; with crests only the heights of the
// levels 1 and up change, by their lifts, and an edit adds its change on top.
void testSamplesWithCrests() {
    const horizon_test::World world;
    HorizonParams points;
    points.crestRatio = 0;
    const HorizonParams crests;
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(world.field);
    const double x = mountain.x * terrain::kChunkSizeMeters;
    const double z = mountain.z * terrain::kChunkSizeMeters;
    const terrain::EditSummarySource raise = [](const terrain::CellFootprint&) {
        return std::optional<terrain::EditSummary>(
            terrain::EditSummary{12.5f, std::nullopt, std::nullopt});
    };
    const terrain::HorizonSampler before(world.field, world.ecology, points);
    const terrain::HorizonSampler after(world.field, world.ecology, crests);
    const terrain::HorizonSampler edited(world.field, world.ecology, crests, raise);
    const terrain::HorizonSampler editedPoints(world.field, world.ecology, points, raise);
    const HorizonCrestSampler lifter(world.field, crests);
    int raised = 0;
    for (const int level : {0, 3, 7}) {
        const HorizonRect rect = around(crests, level, x, z, 5);
        const std::vector<HorizonCell> a = before.sample(level, rect);
        const std::vector<HorizonCell> b = after.sample(level, rect);
        const std::vector<HorizonCell> e = edited.sample(level, rect);
        const std::vector<HorizonCell> f = editedPoints.sample(level, rect);
        const std::vector<float> lifts = lifter.sample(level, rect);
        std::size_t n = 0;
        for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
            for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
                const int pitch = terrain::levelPitch(crests, level);
                const float ground = world.field.heightAt(terrain::samplePosition(i, pitch),
                                                          terrain::samplePosition(k, pitch));
                TEST_CHECK(a[n].height == ground);
                const float lifted = lifts[n] > 0.0f ? ground + lifts[n] : ground;
                TEST_CHECK(b[n].height == lifted && e[n].height == lifted + 12.5f);
                HorizonCell same = b[n];
                same.height = a[n].height;
                TEST_CHECK(same == a[n]);
                // The edits shape the gradients as without crests.
                TEST_CHECK(f[n].height == ground + 12.5f);
                TEST_CHECK(e[n].gradX == f[n].gradX && e[n].gradZ == f[n].gradZ);
                raised += lifts[n] > 0.0f ? 1 : 0;
            }
        }
    }
    TEST_CHECK(raised > 0);
}

void testInvalid() {
    const horizon_test::World world;
    for (const int ratio : {0, 2, 4, 8, 16}) {
        HorizonParams params;
        params.crestRatio = ratio;
        terrain::validate(params);
    }
    for (const int ratio : {-2, 1, 3, 6, 32}) {
        HorizonParams params;
        params.crestRatio = ratio;
        bool threw = false;
        try {
            terrain::validate(params);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST_CHECK(threw);
    }
    HorizonParams fine;
    fine.basePitch = 4;
    fine.crestRatio = 16; // finer than 1 m on level 1
    bool threw = false;
    try {
        terrain::validate(fine);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
    const HorizonParams params;
    const HorizonCrestSampler sampler(world.field, params);
    const HorizonRect rect{0, 0, 4, 3};
    std::vector<float> lifts(12);
    std::vector<float> wrong(20);
    for (const auto& call : std::vector<std::function<void()>>{
             [&] { sampler.fill(-1, rect, std::vector<float>(30), lifts); },
             [&] { sampler.fill(params.levels, rect, std::vector<float>(30), lifts); },
             [&] { sampler.fill(1, rect, wrong, lifts); },
             [&] { sampler.fill(1, rect, std::vector<float>(30), wrong); },
             [&] {
                 const terrain::Heightfield other(horizon_test::kSeed, {},
                                                  terrain::WorldSize{65'536.0 * 64, 65'536.0 * 32});
                 const HorizonCrestSampler bad(other, params);
             }}) {
        bool rejected = false;
        try {
            call();
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        TEST_CHECK(rejected);
    }
}

} // namespace

int main() {
    testOracle();
    testLevelZeroAndSea();
    testRectangles();
    testWorldEdges();
    testSamplesWithCrests();
    testInvalid();
    return test::result();
}
