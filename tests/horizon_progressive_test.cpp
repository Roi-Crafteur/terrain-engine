// The far horizon brought to a new place level by level, finest first (agent 38,
// HorizonClipmap::planFinest): the same samples as one epoch of every level, the coarser levels
// left as they are until their turn, the invalidations kept until every level computes them,
// and the arguments refused.
#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <optional>
#include <stdexcept>
#include <vector>

namespace {

using terrain::HorizonClipmap;
using terrain::HorizonEpoch;

// Plans, computes and commits the epoch of the finest levels; false if there was none.
bool stage(HorizonClipmap& clipmap, const terrain::HorizonSampler& sampler, double x, double z,
           int levels) {
    std::optional<HorizonEpoch> epoch = clipmap.planFinest(x, z, levels);
    if (!epoch) {
        return false;
    }
    TEST_CHECK(static_cast<int>(epoch->centers.size()) == levels);
    for (const terrain::HorizonPiece& piece : epoch->pieces) {
        TEST_CHECK(piece.level < levels);
    }
    horizon_test::computeAll(sampler, *epoch);
    clipmap.commit(*epoch);
    return true;
}

// From nothing, one more level at each epoch: the coarser levels unplaced until their turn, then
// the very samples of one epoch of every level.
void testLevelByLevel() {
    const horizon_test::World world;
    const terrain::HorizonParams params = horizon_test::smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    HorizonClipmap clipmap(params);
    const double x = horizon_test::kX;
    const double z = horizon_test::kZ;
    for (int levels = 1; levels <= params.levels; ++levels) {
        TEST_CHECK(stage(clipmap, sampler, x, z, levels));
        for (int level = 0; level < params.levels; ++level) {
            TEST_CHECK(clipmap.level(level).placed() == (level < levels));
        }
    }
    TEST_CHECK(horizon_test::sameAsFresh(clipmap, sampler, x, z));
    TEST_CHECK(!clipmap.planFinest(x, z, 3).has_value());
    TEST_CHECK(!clipmap.plan(x, z).has_value());
}

// After a jump, the finest levels move first and the coarser ones keep their windows until their
// turn; the end is the horizon of a fresh clipmap at the new place.
void testJump() {
    const horizon_test::World world;
    const terrain::HorizonParams params = horizon_test::smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    HorizonClipmap clipmap(params);
    const double x = horizon_test::kX;
    const double z = horizon_test::kZ;
    horizon_test::settle(clipmap, sampler, x, z);
    std::vector<terrain::HorizonIndex> before;
    for (int level = 0; level < params.levels; ++level) {
        before.push_back(clipmap.level(level).center());
    }
    const double far = x + 300'000.0;
    TEST_CHECK(stage(clipmap, sampler, far, z, 3));
    for (int level = 3; level < params.levels; ++level) {
        TEST_CHECK(clipmap.level(level).center() == before[static_cast<std::size_t>(level)]);
    }
    TEST_CHECK(clipmap.level(0).heightAt(far, z).has_value());
    TEST_CHECK(stage(clipmap, sampler, far, z, params.levels));
    TEST_CHECK(horizon_test::sameAsFresh(clipmap, sampler, far, z));
}

// An invalidated area is computed again by the epochs of the finest levels on those levels, but
// stays pending until an epoch of every level computes it on all of them.
void testInvalidationsKept() {
    const horizon_test::World world;
    const terrain::HorizonParams params = horizon_test::smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    HorizonClipmap clipmap(params);
    const double x = horizon_test::kX;
    const double z = horizon_test::kZ;
    horizon_test::settle(clipmap, sampler, x, z);
    clipmap.invalidate(x - 20.0, z - 20.0, x + 20.0, z + 20.0);
    std::optional<HorizonEpoch> partial = clipmap.planFinest(x, z, 2);
    TEST_CHECK(partial.has_value() && !partial->pieces.empty());
    horizon_test::computeAll(sampler, *partial);
    clipmap.commit(*partial);
    TEST_CHECK(clipmap.pendingInvalidations() == 1);
    std::optional<HorizonEpoch> full = clipmap.plan(x, z);
    TEST_CHECK(full.has_value());
    horizon_test::computeAll(sampler, *full);
    clipmap.commit(*full);
    TEST_CHECK(clipmap.pendingInvalidations() == 0);
    TEST_CHECK(horizon_test::sameAsFresh(clipmap, sampler, x, z));
}

template <typename Call>
bool throwsInvalid(Call call) {
    try {
        call();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void testInvalid() {
    const horizon_test::World world;
    const terrain::HorizonParams params = horizon_test::smallParams();
    HorizonClipmap clipmap(params);
    TEST_CHECK(throwsInvalid([&] { static_cast<void>(clipmap.planFinest(0.0, 0.0, 0)); }));
    TEST_CHECK(throwsInvalid(
        [&] { static_cast<void>(clipmap.planFinest(0.0, 0.0, params.levels + 1)); }));
    // A piece of a level the epoch does not move.
    HorizonEpoch epoch = *clipmap.planFinest(0.0, 0.0, 1);
    epoch.pieces.push_back({1, {0, 0, 1, 1}, std::vector<terrain::HorizonCell>(1)});
    TEST_CHECK(throwsInvalid([&] { clipmap.commit(epoch); }));
}

} // namespace

int main() {
    testLevelByLevel();
    testJump();
    testInvalidationsKept();
    testInvalid();
    return test::result();
}
