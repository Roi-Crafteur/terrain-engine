// Levels of the far horizon in their toroidal buffers, updated by epochs (plan, computePiece,
// commit) on one thread: a move by one sample of level 0 computes one band of it, and the
// result is the one of a full computation; a walk computes far fewer samples than full
// computations; crossing an edge of the world or jumping by a whole world computes no full
// level; the origins stay at the image nearest to the observer even when it crossed an edge
// while an epoch was computed; two observers read the same samples; the geomorphing target
// along the border of a level is the next coarser level (its even samples with point samples,
// raised samples with crests); invalidated areas are computed again,
// those invalidated during an epoch included.
#include "terrain/streaming/horizon_epoch.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

using terrain::HorizonCell;
using terrain::HorizonClipmap;
using terrain::HorizonEpoch;
using terrain::HorizonIndex;
using terrain::HorizonParams;
using terrain::HorizonPiece;
using terrain::HorizonRect;
using terrain::HorizonSampler;

using horizon_test::computeAll;
using horizon_test::fullSamples;
using horizon_test::kX;
using horizon_test::kZ;
using horizon_test::sameAsFresh;
using horizon_test::settle;
using horizon_test::smallParams;
using horizon_test::World;

void testOneSampleMove() {
    const World world;
    const HorizonParams params = smallParams();
    const HorizonSampler sampler(world.field, world.ecology, params);
    HorizonClipmap clipmap(params);
    TEST_CHECK(settle(clipmap, sampler, kX, kZ) == fullSamples(params));
    TEST_CHECK(!clipmap.plan(kX, kZ).has_value());

    // 8 m east: the center of level 0 moves by 2 samples, no other level moves.
    std::optional<HorizonEpoch> epoch = clipmap.plan(kX + 8.0, kZ);
    TEST_CHECK(epoch.has_value());
    if (!epoch) {
        return;
    }
    TEST_CHECK(epoch->samples() == 2 * terrain::samplesPerSide(params));
    for (const HorizonPiece& piece : epoch->pieces) {
        TEST_CHECK(piece.level == 0 && piece.rect.width() == 2);
    }
    computeAll(sampler, *epoch);
    const terrain::HorizonChanges changes = clipmap.commit(*epoch);
    TEST_CHECK(changes.levels.size() == 1 && changes.levels[0].level == 0);
    TEST_CHECK(changes.levels[0].moved);
    TEST_CHECK(sameAsFresh(clipmap, sampler, kX + 8.0, kZ));
    // 2 m more: no window moves.
    TEST_CHECK(!clipmap.plan(kX + 10.0, kZ).has_value());
}

void testWalk() {
    const World world;
    const HorizonParams params = smallParams();
    const HorizonSampler sampler(world.field, world.ecology, params);
    HorizonClipmap clipmap(params);
    double x = kX;
    double z = kZ;
    std::int64_t samples = settle(clipmap, sampler, x, z);
    for (int step = 0; step < 40; ++step) {
        x += 11.0 + 13.0 * (step % 7);
        z += 17.0 * (step % 5) - 34.0;
        samples += settle(clipmap, sampler, x, z);
    }
    std::printf("walk of 40 steps: %lld samples computed, %lld for one full computation\n",
                static_cast<long long>(samples), static_cast<long long>(fullSamples(params)));
    TEST_CHECK(samples < 3 * fullSamples(params));
    TEST_CHECK(sameAsFresh(clipmap, sampler, x, z));
}

void testWorldEdges() {
    const World world;
    const HorizonParams params = smallParams();
    const HorizonSampler sampler(world.field, world.ecology, params);
    const double ex = params.world.x / 2.0;
    const double ez = params.world.z / 2.0;
    // Across the east edge, then across the corner where the four edges meet.
    for (const auto& [from, to] :
         {std::pair{std::pair{ex - 20.0, kZ}, std::pair{ex + 20.0, kZ}},
          std::pair{std::pair{ex - 20.0, ez - 20.0}, std::pair{-ex + 20.0, -ez + 20.0}}}) {
        HorizonClipmap clipmap(params);
        settle(clipmap, sampler, from.first, from.second);
        const std::int64_t samples = settle(clipmap, sampler, to.first, to.second);
        std::printf("across an edge of the world: %lld samples computed\n",
                    static_cast<long long>(samples));
        TEST_CHECK(samples > 0 && samples < fullSamples(params) / 8);
        TEST_CHECK(sameAsFresh(clipmap, sampler, to.first, to.second));
        // The same observer one world away: nothing to compute.
        TEST_CHECK(!clipmap.plan(to.first + params.world.x, to.second - params.world.z));
    }
}

// The observer crosses the east edge while the epoch planned before is computed: the origins
// of the committed windows are given at the images nearest to the observer.
void testOriginsAcrossEdge() {
    const World world;
    const HorizonParams params = smallParams();
    const HorizonSampler sampler(world.field, world.ecology, params);
    const double before = params.world.x / 2.0 - 100.0;
    const double after = -params.world.x / 2.0 + 100.0; // 200 m east, on the other side
    HorizonClipmap clipmap(params);
    std::optional<HorizonEpoch> epoch = clipmap.plan(before, kZ);
    TEST_CHECK(epoch.has_value());
    computeAll(sampler, *epoch);
    clipmap.commit(*epoch);
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        const terrain::HorizonLevel& level = clipmap.level(n);
        const double center = level.originX(after) + params.cellsPerSide / 2 * level.pitch();
        TEST_CHECK(std::abs(center - after) <= 200.0 + level.pitch());
        const double fromBefore = level.originX(before) + params.cellsPerSide / 2 * level.pitch();
        TEST_CHECK(std::abs(fromBefore - before) <= level.pitch());
        // The samples read at the image of the observer are the samples there.
        const double sampleX = level.originX(after) + 3.0 * level.pitch();
        const double sampleZ = level.originZ(kZ) + 5.0 * level.pitch();
        const std::optional<float> height = level.heightAt(sampleX, sampleZ);
        const terrain::HorizonIndex there = terrain::canonicalSample(
            {std::llround(sampleX / level.pitch()), std::llround(sampleZ / level.pitch())},
            level.counts());
        const std::vector<HorizonCell> ground =
            sampler.sample(n, {there.x, there.z, there.x + 1, there.z + 1});
        TEST_CHECK(height && *height == ground.front().height);
    }
}

void testTwoObservers() {
    const World world;
    const HorizonParams params = smallParams();
    const HorizonSampler sampler(world.field, world.ecology, params);
    HorizonClipmap a(params);
    HorizonClipmap b(params);
    settle(a, sampler, kX, kZ);
    settle(b, sampler, kX + 3000.0, kZ - 2000.0);
    int shared = 0;
    for (int n = 0; n < params.levels; ++n) {
        const HorizonRect window = a.level(n).window();
        for (std::int64_t k = window.z0; k < window.z1; ++k) {
            for (std::int64_t i = window.x0; i < window.x1; ++i) {
                const HorizonCell* other = b.level(n).find({i, k});
                if (other != nullptr) {
                    ++shared;
                    TEST_CHECK(*other == a.level(n).at({i, k}));
                }
            }
        }
    }
    TEST_CHECK(shared > params.levels * 64 * 32);
}

// Along the border of level n, the geomorphing target is the surface of level n + 1: its sample
// at the even samples, the mean of its two samples around the odd ones. With point samples
// (crestRatio 0) its samples are the even samples of level n, bit for bit; with crests they
// differ where the crests lift them: the renderer morphs toward the coarser samples.
void testMorphTargets(int crestRatio) {
    const World world;
    HorizonParams params = smallParams();
    params.crestRatio = crestRatio;
    const HorizonSampler sampler(world.field, world.ecology, params);
    HorizonClipmap clipmap(params);
    settle(clipmap, sampler, kX, kZ);
    int differ = 0;
    for (int n = 0; n + 1 < params.levels; ++n) {
        const terrain::HorizonLevel& fine = clipmap.level(n);
        const terrain::HorizonLevel& coarse = clipmap.level(n + 1);
        for (int i = 1; i + 1 < fine.side(); ++i) {
            for (const int k : {0, fine.side() - 1}) { // the north and south borders
                const double x = fine.originX(kX) + i * fine.pitch();
                const double z = fine.originZ(kZ) + k * fine.pitch();
                const std::optional<float> target = coarse.heightAt(x, z);
                const double u = (x - coarse.originX(kX)) / coarse.pitch();
                const auto v = static_cast<int>((z - coarse.originZ(kZ)) / coarse.pitch());
                const auto at = [&](double a) {
                    return static_cast<double>(coarse.atWindow(static_cast<int>(a), v).height);
                };
                const double expected = i % 2 == 0 ? at(u) : 0.5 * (at(u - 0.5) + at(u + 0.5));
                TEST_CHECK(target && *target == static_cast<float>(expected));
                if (i % 2 == 0) {
                    const bool same = at(u) == fine.atWindow(i, k).height;
                    TEST_CHECK(crestRatio != 0 || same);
                    differ += same ? 0 : 1;
                }
            }
        }
    }
    std::printf("morph targets, crest ratio %d: %d even border samples differ from the coarser "
                "level\n",
                crestRatio, differ);
    TEST_CHECK(crestRatio == 0 || differ > 0);
}

// Edits raising the ground by a value the test changes, over an area.
struct Edits {
    float raise = 0.0f;
};

void testInvalidations() {
    const World world;
    const HorizonParams params = smallParams();
    auto edits = std::make_shared<Edits>();
    const terrain::EditSummarySource source = [edits](const terrain::CellFootprint& footprint) {
        const bool inside =
            std::abs(footprint.centerX - kX) < 300.0 && std::abs(footprint.centerZ - kZ) < 300.0;
        return inside && edits->raise != 0.0f
                   ? std::optional<terrain::EditSummary>{{edits->raise, std::nullopt}}
                   : std::nullopt;
    };
    const HorizonSampler sampler(world.field, world.ecology, params, source);
    HorizonClipmap clipmap(params);
    settle(clipmap, sampler, kX, kZ);

    edits->raise = 30.0f;
    clipmap.invalidate(kX - 300.0, kZ - 300.0, kX + 300.0, kZ + 300.0);
    std::optional<HorizonEpoch> epoch = clipmap.plan(kX, kZ);
    TEST_CHECK(epoch.has_value());
    if (!epoch) {
        return;
    }
    std::printf("an invalidated area of 600 m: %lld samples computed again\n",
                static_cast<long long>(epoch->samples()));
    TEST_CHECK(epoch->samples() < fullSamples(params) / 4);
    computeAll(sampler, *epoch);
    // Invalidated while the epoch is computed: kept for the next one.
    edits->raise = -12.0f;
    clipmap.invalidate(kX - 300.0, kZ - 300.0, kX + 300.0, kZ + 300.0);
    clipmap.commit(*epoch);
    TEST_CHECK(clipmap.pendingInvalidations() == 1);
    settle(clipmap, sampler, kX, kZ);
    TEST_CHECK(clipmap.pendingInvalidations() == 0);
    TEST_CHECK(sameAsFresh(clipmap, sampler, kX, kZ));
    const HorizonCell* raised = clipmap.level(0).find(terrain::canonicalCenter(params, 0, kX, kZ));
    TEST_CHECK(raised && (raised->flags & terrain::kHorizonEdited) != 0);
}

void testErrors() {
    const World world;
    const HorizonParams params = smallParams();
    const HorizonSampler sampler(world.field, world.ecology, params);
    HorizonClipmap clipmap(params);
    const auto throws = [](auto&& action) {
        try {
            action();
        } catch (const std::invalid_argument&) {
            return true;
        } catch (const std::out_of_range&) {
            return true;
        }
        return false;
    };
    std::optional<HorizonEpoch> first = clipmap.plan(kX, kZ);
    TEST_CHECK(first.has_value());
    TEST_CHECK(throws([&] { clipmap.commit(*first); })); // pieces not computed
    computeAll(sampler, *first);
    clipmap.commit(*first);
    TEST_CHECK(throws([&] { clipmap.commit(*first); })); // stale
    TEST_CHECK(throws([&] { static_cast<void>(clipmap.plan(std::nan(""), 0.0)); }));
    TEST_CHECK(throws([&] { static_cast<void>(clipmap.plan(0.0, 0.0, 0)); }));
    TEST_CHECK(throws([&] { clipmap.invalidate(1.0, 0.0, 0.0, 1.0); }));
    TEST_CHECK(throws([&] { clipmap.invalidate(0.0, 0.0, params.world.x, 1.0); }));
    TEST_CHECK(throws([&] { static_cast<void>(clipmap.level(params.levels)); }));
    TEST_CHECK(throws([&] { static_cast<void>(clipmap.level(0).atWindow(-1, 0)); }));
    TEST_CHECK(!clipmap.level(0).heightAt(kX + 1.0e6, kZ).has_value());
    TEST_CHECK(clipmap.memoryBytes() ==
               static_cast<std::size_t>(fullSamples(params)) * sizeof(terrain::HorizonCell));
}

} // namespace

int main() {
    testOneSampleMove();
    testWalk();
    testWorldEdges();
    testOriginsAcrossEdge();
    testTwoObservers();
    testMorphTargets(0);
    testMorphTargets(HorizonParams{}.crestRatio);
    testInvalidations();
    testErrors();
    return test::result();
}
