// The far horizon computed by worker threads: the same samples once idle with 0, 1 or 4
// workers and whatever path the observer took (a jump, a walk updated while epochs are
// computed, a walk across the edge of the world); the epochs driven by hand, their pieces
// computed backwards on other threads, give the same samples; the levels are nested after every
// update; an invalidation made while an epoch is computed is not lost; an exception thrown by a
// piece is rethrown, then the streamer recovers.
#include "terrain/streaming/horizon_streamer.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using horizon_test::kX;
using horizon_test::kZ;
using horizon_test::sameContent;
using horizon_test::settle;
using horizon_test::smallParams;
using horizon_test::World;
using terrain::HorizonStreamer;
using terrain::HorizonStreamParams;

struct Point {
    double x;
    double z;
};

// Walks from a point to another in steps of about 60 m, updating at every step without waiting.
void walk(HorizonStreamer& streamer, Point from, Point to) {
    const double dx = to.x - from.x;
    const double dz = to.z - from.z;
    const int steps = static_cast<int>(std::sqrt(dx * dx + dz * dz) / 60.0) + 1;
    for (int step = 0; step <= steps; ++step) {
        const double t = static_cast<double>(step) / steps;
        streamer.update(from.x + dx * t, from.z + dz * t);
        if (step % 3 == 0) {
            streamer.generatePending(2); // without workers: some progress between updates
        }
    }
}

void testThreadsAndPaths() {
    const World world;
    const terrain::HorizonParams params = smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    const double edge = params.world.x / 2.0;
    const Point target{-edge + 300.0, kZ}; // just across the east edge, seen from the west
    terrain::HorizonClipmap reference(params);
    horizon_test::settle(reference, sampler, target.x, target.z);
    for (const int threads : {0, 1, 4}) {
        for (int path = 0; path < 3; ++path) {
            HorizonStreamer streamer(sampler, {threads, 1024});
            if (path == 1) {
                settle(streamer, target.x - 2000.0, target.z + 700.0);
                walk(streamer, {target.x - 2000.0, target.z + 700.0}, target);
            } else if (path == 2) { // across the edge, ending one world east of the target
                settle(streamer, edge - 900.0, target.z);
                walk(streamer, {edge - 900.0, target.z}, {target.x + params.world.x, target.z});
            }
            const Point last = path == 2 ? Point{target.x + params.world.x, target.z} : target;
            settle(streamer, last.x, last.z);
            TEST_CHECK(!streamer.busy());
            TEST_CHECK(sameContent(streamer.clipmap(), reference));
        }
    }
}

// The epochs of a clipmap driven by hand, as a renderer with its own tasks does: the pieces
// computed backwards, by two threads.
void testEpochsByHand() {
    const World world;
    const terrain::HorizonParams params = smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    terrain::HorizonClipmap clipmap(params);
    for (const Point p : {Point{kX, kZ}, Point{kX + 700.0, kZ - 300.0}}) {
        std::optional<terrain::HorizonEpoch> epoch = clipmap.plan(p.x, p.z, 512);
        TEST_CHECK(epoch.has_value());
        if (!epoch) {
            return;
        }
        std::atomic<std::size_t> next{0};
        const auto task = [&] {
            for (std::size_t n = next++; n < epoch->pieces.size(); n = next++) {
                terrain::computePiece(sampler, epoch->pieces[epoch->pieces.size() - 1 - n]);
            }
        };
        std::thread a(task);
        std::thread b(task);
        a.join();
        b.join();
        clipmap.commit(*epoch);
    }
    HorizonStreamer streamer(sampler, {2, terrain::kHorizonPieceSamples});
    settle(streamer, kX + 700.0, kZ - 300.0);
    TEST_CHECK(sameContent(streamer.clipmap(), clipmap));
}

// Every level lies inside the next coarser one, at the images nearest to the observer.
bool nested(const terrain::HorizonClipmap& clipmap, Point observer) {
    for (int n = 0; n + 1 < clipmap.levelCount(); ++n) {
        const terrain::HorizonLevel& fine = clipmap.level(n);
        const terrain::HorizonLevel& coarse = clipmap.level(n + 1);
        if (!fine.placed() || !coarse.placed()) {
            return fine.placed() == coarse.placed();
        }
        const double fineSide = (fine.side() - 1.0) * fine.pitch();
        const double coarseSide = (coarse.side() - 1.0) * coarse.pitch();
        for (const auto& [a, b] :
             {std::pair{fine.originX(observer.x), coarse.originX(observer.x)},
              std::pair{fine.originZ(observer.z), coarse.originZ(observer.z)}}) {
            if (a < b || a + fineSide > b + coarseSide) {
                return false;
            }
        }
    }
    return true;
}

void testNestedWhileWalking() {
    const World world;
    const terrain::HorizonParams params = smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    HorizonStreamer streamer(sampler, {4, 1024});
    settle(streamer, kX, kZ);
    bool always = true;
    for (int step = 1; step <= 60; ++step) {
        const Point p{kX + 45.0 * step, kZ + 20.0 * step};
        const terrain::HorizonChanges changes = streamer.update(p.x, p.z);
        always = always && nested(streamer.clipmap(), p);
        for (const terrain::HorizonLevelChange& change : changes.levels) {
            TEST_CHECK(change.moved || !change.written.empty());
        }
    }
    TEST_CHECK(always);
}

// Edits whose height the test changes, and which can fail.
struct Edits {
    std::atomic<float> raise{0.0f};
    std::atomic<bool> fail{false};
};

terrain::EditSummarySource editSource(const std::shared_ptr<Edits>& edits) {
    return [edits](const terrain::CellFootprint& footprint) {
        const bool inside =
            std::abs(footprint.centerX - kX) < 400.0 && std::abs(footprint.centerZ - kZ) < 400.0;
        if (inside && edits->fail) {
            throw std::runtime_error("edit summaries unavailable");
        }
        const float raise = edits->raise;
        return inside && raise != 0.0f ? std::optional<terrain::EditSummary>{{raise, std::nullopt}}
                                       : std::nullopt;
    };
}

void testInvalidationsAndErrors() {
    const World world;
    const terrain::HorizonParams params = smallParams();
    for (const int threads : {0, 3}) {
        const auto edits = std::make_shared<Edits>();
        const terrain::HorizonSampler sampler(world.field, world.ecology, params,
                                              editSource(edits));
        HorizonStreamer streamer(sampler, {threads, 512});
        settle(streamer, kX, kZ);

        // Invalidated while the epoch of the first change is computed: not lost.
        edits->raise = 25.0f;
        streamer.invalidate(kX - 400.0, kZ - 400.0, kX + 400.0, kZ + 400.0);
        streamer.update(kX, kZ);
        TEST_CHECK(streamer.busy());
        edits->raise = -8.0f;
        streamer.invalidate(kX - 400.0, kZ - 400.0, kX + 400.0, kZ + 400.0);
        settle(streamer, kX, kZ);
        TEST_CHECK(streamer.clipmap().pendingInvalidations() == 0);
        TEST_CHECK(horizon_test::sameAsFresh(streamer.clipmap(), sampler, kX, kZ));

        // A piece that throws: rethrown by waitIdle, the invalidation is kept, then recovered.
        edits->fail = true;
        streamer.invalidate(kX - 400.0, kZ - 400.0, kX + 400.0, kZ + 400.0);
        streamer.update(kX, kZ);
        bool threw = false;
        try {
            streamer.waitIdle();
        } catch (const std::runtime_error&) {
            threw = true;
        }
        TEST_CHECK(threw && !streamer.busy());
        TEST_CHECK(streamer.clipmap().pendingInvalidations() == 1);
        edits->fail = false;
        edits->raise = 40.0f;
        settle(streamer, kX, kZ);
        TEST_CHECK(streamer.clipmap().pendingInvalidations() == 0);
        TEST_CHECK(horizon_test::sameAsFresh(streamer.clipmap(), sampler, kX, kZ));
    }
}

void testInvalidParameters() {
    const World world;
    const terrain::HorizonSampler sampler(world.field, world.ecology, smallParams());
    for (const HorizonStreamParams params :
         {HorizonStreamParams{-1, 4096}, HorizonStreamParams{2, 0}}) {
        bool threw = false;
        try {
            HorizonStreamer streamer(sampler, params);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST_CHECK(threw);
    }
}

} // namespace

int main() {
    testThreadsAndPaths();
    testEpochsByHand();
    testNestedWhileWalking();
    testInvalidationsAndErrors();
    testInvalidParameters();
    return test::result();
}
