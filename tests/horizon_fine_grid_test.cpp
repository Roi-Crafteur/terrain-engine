// The fine grid of the far horizon: its nodes are Heightfield::heightAt at their canonical
// positions, bit for bit, whatever the ratio, the box, the nodes asked for and the order they are
// read in, across the edges and the corner of the wrapped world; the detail slopes and the crest
// lifts read from a grid (of their ratio or of a multiple of it, alone or after other nodes) are
// those of HorizonNormalSampler::sample and HorizonCrestSampler::sample; invalid grids.
#include "terrain/streaming/horizon_crests.hpp"
#include "terrain/streaming/horizon_fine_grid.hpp"
#include "terrain/streaming/horizon_normals.hpp"

#include "horizon_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using terrain::HorizonFineGrid;
using terrain::HorizonIndex;
using terrain::HorizonParams;
using terrain::HorizonRect;

// The node (u, v) of a level at a ratio, by heightAt.
float oracle(const terrain::Heightfield& field, const HorizonParams& params, int level, int ratio,
             std::int64_t u, std::int64_t v) {
    const int spacing = terrain::levelPitch(params, level) / ratio;
    const HorizonIndex counts = terrain::levelCounts(params, level);
    return field.heightAt(
        terrain::samplePosition(terrain::wrapIndex(u, counts.x * ratio), spacing),
        terrain::samplePosition(terrain::wrapIndex(v, counts.z * ratio), spacing));
}

HorizonRect around(const HorizonParams& params, int level, double x, double z, int half) {
    const HorizonIndex c = terrain::canonicalCenter(params, level, x, z);
    return {c.x - half, c.z - half, c.x + half, c.z + half};
}

// Mountains of the default world, and the corner of the world on land or sea.
std::vector<std::pair<double, double>> places(const horizon_test::World& world) {
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(world.field);
    const terrain::WorldSize size;
    return {{mountain.x * terrain::kChunkSizeMeters, mountain.z * terrain::kChunkSizeMeters},
            {size.x / 2.0, size.z / 2.0}};
}

// Every node of a box read at once, by rows read alone, or the even nodes then the others.
void testNodes(const horizon_test::World& world) {
    const HorizonParams params;
    for (const auto& [x, z] : places(world)) {
        for (const int level : {0, 3, 9}) {
            for (const int ratio : {1, 2, 4, 8, 16}) {
                if (terrain::levelPitch(params, level) % ratio != 0) {
                    continue;
                }
                const HorizonRect samples = around(params, level, x, z, 2);
                const HorizonRect box{samples.x0 * ratio - 1, samples.z0 * ratio - 2,
                                      samples.x1 * ratio + 1, samples.z0 * ratio + 5};
                HorizonFineGrid whole(params, level, ratio, box);
                whole.read(world.field, box);
                HorizonFineGrid parts(params, level, ratio, box);
                const auto even = [](std::int64_t u, std::int64_t v) { return (u + v) % 2 == 0; };
                parts.readIf(world.field, even);
                parts.readIf(world.field,
                             [&](std::int64_t u, std::int64_t v) { return !even(u, v); });
                bool same = true;
                for (std::int64_t v = box.z0; v < box.z1; ++v) {
                    for (std::int64_t u = box.x0; u < box.x1; ++u) {
                        const float expected = oracle(world.field, params, level, ratio, u, v);
                        same = same && whole.at(u, v) == expected && parts.at(u, v) == expected;
                    }
                }
                TEST_CHECK(same);
                TEST_CHECK(whole.spacing() * ratio == terrain::levelPitch(params, level));
            }
        }
    }
}

// The nodes not asked for keep 0; a node of a finer grid is the node of a coarser one.
void testWanted(const horizon_test::World& world) {
    const HorizonParams params;
    const HorizonRect samples = around(params, 4, places(world).front().first,
                                       places(world).front().second, 3);
    const HorizonRect box{samples.x0 * 8, samples.z0 * 8, samples.x1 * 8, samples.z1 * 8};
    HorizonFineGrid fine(params, 4, 8, box);
    fine.readIf(world.field, [](std::int64_t u, std::int64_t) { return u % 3 == 0; });
    HorizonFineGrid coarse(params, 4, 2, {samples.x0 * 2, samples.z0 * 2, samples.x1 * 2,
                                          samples.z1 * 2});
    coarse.read(world.field, coarse.box());
    bool kept = true;
    bool nested = true;
    for (std::int64_t v = box.z0; v < box.z1; ++v) {
        for (std::int64_t u = box.x0; u < box.x1; ++u) {
            kept = kept && (u % 3 == 0 || fine.at(u, v) == 0.0f);
            if (u % 12 == 0 && v % 4 == 0) {
                nested = nested && fine.at(u, v) == coarse.at(u / 4, v / 4);
            }
        }
    }
    TEST_CHECK(kept && nested);
    TEST_CHECK(fine.memoryBytes() == static_cast<std::size_t>(box.count()) * sizeof(float));
}

// The detail slopes from a grid of their ratio and of twice it equal those of sample().
void testNormalsFromGrid(const horizon_test::World& world) {
    const HorizonParams params;
    for (const int ratio : {2, 4}) {
        const terrain::HorizonNormalSampler sampler(world.field, params, ratio);
        for (const int level : {0, 3, 6}) {
            const HorizonRect rect = around(params, level, horizon_test::kX, horizon_test::kZ, 2);
            const std::vector<terrain::DetailSlope> expected = sampler.sample(level, rect);
            for (const int stride : {1, 2}) {
                const HorizonRect nodes = sampler.nodes(rect);
                const HorizonRect box{nodes.x0 * stride, nodes.z0 * stride,
                                      (nodes.x1 - 1) * stride + 1, (nodes.z1 - 1) * stride + 1};
                HorizonFineGrid grid(params, level, ratio * stride, box);
                grid.readIf(world.field, [&](std::int64_t u, std::int64_t v) {
                    return u % stride == 0 && v % stride == 0;
                });
                std::vector<terrain::DetailSlope> slopes(expected.size());
                sampler.fill(level, rect, grid, slopes);
                TEST_CHECK(slopes == expected);
            }
        }
    }
}

// The lifts from a grid of their ratio, of twice and four times it, read alone or after a dense
// box of nodes taken as held, equal those of sample(), on levels 1 to 9 and at the corner.
int crestsFromGrid(const horizon_test::World& world, int ratio, int level, double x, double z) {
    HorizonParams params;
    params.crestRatio = ratio;
    const terrain::HorizonCrestSampler sampler(world.field, params);
    const HorizonRect rect = around(params, level, x, z, 3);
    const std::vector<float> expected = sampler.sample(level, rect);
    int raised = 0;
    if (!sampler.raises(level)) {
        for (const float lift : expected) {
            TEST_CHECK(lift == 0.0f);
        }
        return 0;
    }
    for (const int stride : {1, 2, 4}) {
        if (ratio * stride > terrain::kHorizonCrestRatioMax) {
            continue;
        }
        const HorizonRect nodes = sampler.nodes(level, rect);
        const HorizonRect box{nodes.x0 * stride, nodes.z0 * stride, (nodes.x1 - 1) * stride + 1,
                              (nodes.z1 - 1) * stride + 1};
        HorizonFineGrid alone(params, level, ratio * stride, box);
        sampler.read(level, rect, alone);
        HorizonFineGrid shared(params, level, ratio * stride, box);
        const HorizonRect held{box.x0 + 3, box.z0 + ratio * stride, box.x1, box.z1 - 2};
        shared.read(world.field, held);
        sampler.read(level, rect, shared, held);
        std::vector<float> a(expected.size());
        std::vector<float> b(expected.size());
        sampler.fill(level, rect, alone, a);
        sampler.fill(level, rect, shared, b);
        TEST_CHECK(a == expected && b == expected);
    }
    for (const float lift : expected) {
        raised += lift > 0.0f ? 1 : 0;
    }
    return raised;
}

void testCrestsFromGrid(const horizon_test::World& world) {
    int raised = 0;
    for (const auto& [x, z] : places(world)) {
        for (const int ratio : {2, 4, 8, 16}) {
            for (const int level : {0, 1, 4, 9}) {
                raised += crestsFromGrid(world, ratio, level, x, z);
            }
        }
    }
    std::printf("crests from grids: %d samples rise\n", raised);
    TEST_CHECK(raised > 0);
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
    TEST_CHECK(throws([&] { HorizonFineGrid(params, 0, 0, {}); }));
    TEST_CHECK(throws([&] { HorizonFineGrid(params, 0, 3, {}); }));
    TEST_CHECK(throws([&] { HorizonFineGrid(params, 0, 16, {}); })); // 8 m / 16
    TEST_CHECK(throws([&] { HorizonFineGrid(params, 10, 1, {}); }));
    TEST_CHECK(!throws([&] { HorizonFineGrid(params, 1, 16, {}); }));
    HorizonFineGrid grid(params, 2, 4, {0, 0, 8, 8});
    TEST_CHECK(throws([&] { grid.read(world.field, HorizonRect{0, 0, 9, 8}); }));
    const terrain::HorizonNormalSampler normals(world.field, params, 4);
    std::vector<terrain::DetailSlope> slopes(16 * 4);
    TEST_CHECK(throws([&] { normals.fill(2, {0, 0, 2, 2}, grid, slopes); })); // margin missing
    TEST_CHECK(throws([&] { normals.fill(3, {1, 1, 2, 2}, grid, std::span(slopes).first(16)); }));
    const terrain::HorizonCrestSampler crests(world.field, params);
    std::vector<float> lifts(4);
    TEST_CHECK(throws([&] { crests.fill(2, {0, 0, 2, 2}, grid, lifts); })); // low cells missing
    HorizonFineGrid single(params, 2, 1, {-8, -8, 8, 8});
    TEST_CHECK(throws([&] { crests.read(2, {1, 1, 2, 2}, single); })); // 1 is no multiple of 2
    TEST_CHECK(!throws([&] { crests.read(0, {0, 0, 2, 2}, single); })); // level 0: nothing to read
}

} // namespace

int main() {
    const horizon_test::World world;
    testNodes(world);
    testWanted(world);
    testNormalsFromGrid(world);
    testCrestsFromGrid(world);
    testInvalid(world);
    return test::result();
}
