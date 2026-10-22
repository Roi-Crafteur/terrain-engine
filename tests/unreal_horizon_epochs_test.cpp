// Epochs of the far horizon run as jobs of a task system (Bridge/horizon_epochs): the levels end
// with the samples of a clipmap settled at once, whatever the launcher (on the calling thread,
// deferred and run backwards, a pool of threads), along a walk across the edge of the world; at
// most maxPiecesInFlight jobs at once; the prepared data of each piece; an error of a job
// rethrown and the epoch planned again; the runner destroyed while jobs run; invalid parameters.
#include "horizon_epochs.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using terrain::unreal::EpochCommit;
using terrain::unreal::HorizonEpochParams;
using terrain::unreal::HorizonEpochRunner;
using terrain::unreal::JobLauncher;

const JobLauncher kInline = [](std::function<void()> job) { job(); };

// Jobs kept until run by hand, the last launched first.
class Deferred {
public:
    [[nodiscard]] JobLauncher launcher() {
        return [this](std::function<void()> job) {
            const std::lock_guard lock(mutex_);
            jobs_.push_back(std::move(job));
        };
    }
    [[nodiscard]] std::size_t queued() {
        const std::lock_guard lock(mutex_);
        return jobs_.size();
    }
    void runAll() {
        for (;;) {
            std::function<void()> job;
            {
                const std::lock_guard lock(mutex_);
                if (jobs_.empty()) {
                    return;
                }
                job = std::move(jobs_.back());
                jobs_.pop_back();
            }
            job();
        }
    }

private:
    std::mutex mutex_;
    std::vector<std::function<void()>> jobs_;
};

// A pool of threads running the jobs in their order; it outlives the runners using it.
class Pool {
public:
    explicit Pool(int threads) {
        for (int i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { loop(); });
        }
    }
    ~Pool() {
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        workers_.clear();
    }
    [[nodiscard]] JobLauncher launcher() {
        return [this](std::function<void()> job) {
            {
                const std::lock_guard lock(mutex_);
                jobs_.push_back(std::move(job));
            }
            ready_.notify_one();
        };
    }

private:
    void loop() {
        std::unique_lock lock(mutex_);
        for (;;) {
            ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) {
                return;
            }
            std::function<void()> job = std::move(jobs_.front());
            jobs_.pop_front();
            lock.unlock();
            job();
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::vector<std::jthread> workers_; // last: joined first
};

// Updates until no epoch is in flight; returns the commits.
std::vector<EpochCommit> settle(HorizonEpochRunner& runner, double x, double z,
                                Deferred* deferred = nullptr) {
    std::vector<EpochCommit> commits;
    for (;;) {
        EpochCommit commit = runner.update(x, z);
        if (!commit.empty()) {
            commits.push_back(std::move(commit));
        }
        if (!runner.busy()) {
            return commits;
        }
        if (deferred != nullptr) {
            deferred->runAll();
        }
        runner.waitIdle();
    }
}

std::vector<float> heights(const terrain::HorizonPiece& piece) {
    std::vector<float> data;
    for (const terrain::HorizonCell& cell : piece.cells) {
        data.push_back(cell.height);
    }
    return data;
}

void testInline() {
    const horizon_test::World world;
    const terrain::HorizonSampler sampler(world.field, world.ecology, horizon_test::smallParams());
    HorizonEpochRunner runner(sampler, kInline);
    const EpochCommit first = runner.update(horizon_test::kX, horizon_test::kZ);
    TEST_CHECK(!first.empty() && !runner.busy()); // computed within the update
    TEST_CHECK(first.changes.levels.size() == 10u);
    TEST_CHECK(horizon_test::sameAsFresh(runner.clipmap(), sampler, horizon_test::kX,
                                         horizon_test::kZ));
    TEST_CHECK(runner.update(horizon_test::kX, horizon_test::kZ).empty());
}

// Jobs run backwards by hand: at most maxPiecesInFlight queued at once, every prepared piece
// holds the samples it wrote.
void testDeferred() {
    const horizon_test::World world;
    const terrain::HorizonSampler sampler(world.field, world.ecology, horizon_test::smallParams());
    Deferred deferred;
    HorizonEpochRunner runner(sampler, deferred.launcher(), heights, HorizonEpochParams{3, 1000});
    TEST_CHECK(runner.update(horizon_test::kX, horizon_test::kZ).empty());
    TEST_CHECK(runner.busy() && deferred.queued() == 3u);
    deferred.runAll();
    const EpochCommit commit = runner.update(horizon_test::kX, horizon_test::kZ);
    TEST_CHECK(!commit.empty() && !runner.busy());
    std::int64_t samples = 0;
    bool same = true;
    for (const terrain::unreal::PreparedPiece& piece : commit.pieces) {
        same = same && piece.data.size() == static_cast<std::size_t>(piece.rect.count());
        std::size_t n = 0;
        for (std::int64_t k = piece.rect.z0; k < piece.rect.z1 && same; ++k) {
            for (std::int64_t i = piece.rect.x0; i < piece.rect.x1; ++i) {
                same = same && piece.data[n++] == runner.level(piece.level).at({i, k}).height;
            }
        }
        samples += piece.rect.count();
    }
    TEST_CHECK(same);
    TEST_CHECK(samples == horizon_test::fullSamples(horizon_test::smallParams()));
    TEST_CHECK(horizon_test::sameAsFresh(runner.clipmap(), sampler, horizon_test::kX,
                                         horizon_test::kZ));
}

// A walk across the east edge of the world with a pool of threads and every step settled or
// not: the levels end as those of a clipmap settled at the end at once.
void testPoolWalk() {
    const horizon_test::World world;
    const terrain::HorizonSampler sampler(world.field, world.ecology, horizon_test::smallParams());
    const terrain::WorldSize size;
    Pool pool(4);
    HorizonEpochRunner runner(sampler, pool.launcher(), {}, HorizonEpochParams{3, 700});
    double x = size.x / 2.0 - 700.0;
    const double z = -1'000.0;
    for (int step = 0; step < 40; ++step) {
        x += 37.0;
        if (step % 3 == 0) {
            settle(runner, x, z);
        } else {
            runner.update(x, z);
        }
    }
    settle(runner, x, z);
    TEST_CHECK(horizon_test::sameAsFresh(runner.clipmap(), sampler, x, z));
}

// An error of a job is rethrown by the next update; the epoch is planned again.
void testErrors() {
    const horizon_test::World world;
    std::atomic<bool> fail{true};
    const terrain::EditSummarySource edits =
        [&fail](const terrain::CellFootprint&) -> std::optional<terrain::EditSummary> {
        if (fail.load()) {
            throw std::runtime_error("edit summaries unavailable");
        }
        return std::nullopt;
    };
    const terrain::HorizonSampler sampler(world.field, world.ecology, horizon_test::smallParams(),
                                          edits);
    Pool pool(2);
    HorizonEpochRunner runner(sampler, pool.launcher());
    runner.update(horizon_test::kX, horizon_test::kZ);
    runner.waitIdle();
    bool thrown = false;
    try {
        runner.update(horizon_test::kX, horizon_test::kZ);
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    TEST_CHECK(thrown && !runner.busy());
    fail = false;
    settle(runner, horizon_test::kX, horizon_test::kZ);
    TEST_CHECK(horizon_test::sameAsFresh(runner.clipmap(), sampler, horizon_test::kX,
                                         horizon_test::kZ));
}

// Destroyed while slow jobs run: it waits for them.
void testDestroyedWhileRunning() {
    const horizon_test::World world;
    const terrain::HorizonSampler sampler(world.field, world.ecology, horizon_test::smallParams());
    Pool pool(3);
    std::atomic<int> prepared{0};
    {
        HorizonEpochRunner runner(
            sampler, pool.launcher(),
            [&prepared](const terrain::HorizonPiece& piece) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                ++prepared;
                return heights(piece);
            },
            HorizonEpochParams{3, 500});
        runner.update(horizon_test::kX, horizon_test::kZ);
    }
    TEST_CHECK(prepared.load() >= 1);
}

void testInvalid() {
    const horizon_test::World world;
    const terrain::HorizonSampler sampler(world.field, world.ecology, horizon_test::smallParams());
    for (const HorizonEpochParams params : {HorizonEpochParams{0, 100}, HorizonEpochParams{1, 0}}) {
        bool refused = false;
        try {
            [[maybe_unused]] const HorizonEpochRunner runner(sampler, kInline, {}, params);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        TEST_CHECK(refused);
    }
    bool refused = false;
    try {
        [[maybe_unused]] const HorizonEpochRunner runner(sampler, JobLauncher{});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
}

} // namespace

int main() {
    testInline();
    testDeferred();
    testPoolWalk();
    testErrors();
    testDestroyedWhileRunning();
    testInvalid();
    return test::result();
}
