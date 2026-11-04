// The computations of the hydrology stopped from another thread (agent 39, work_stop.hpp), under
// load (as many spinning threads as working ones), on the small world:
// - 4 threads computing courses, their flag raised at drawn instants: every one leaves by
//   WorkStopped, none finishes the course it was making; the median time to their last exit is
//   100 ms at most, and the longest a quarter of the time the courses in progress then take to
//   be finished, at most (measured in the same trial, on the same caches);
// - a thread waiting for a course another thread makes leaves as soon (the waits of the caches);
// - a stopped thread leaves nothing half made: the courses then computed are those of a
//   hydrology never stopped (the bits of hydrology_bits_test);
// - a thread stopped leaves the others computing, their results unchanged.
// The times are printed; only the median has a bound in milliseconds (a loaded machine).
#include "hydrology_sample_support.hpp"

#include "terrain/work_stop.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using terrain::Hydrology;
using terrain::WorkStopped;
using terrain::WorkStopScope;

constexpr int kWorkers = 4;
constexpr int kTrials = 10;
// The checksum of the sample reaches (hydrology_bits_test, pinned on main).
constexpr std::uint64_t kSampleReaches = 0xF1684975E5044E29ULL;

double msBetween(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// kWorkers threads spinning while it lives: the machine under load.
class Load {
public:
    Load() {
        for (int t = 0; t < kWorkers; ++t) {
            threads_.emplace_back([this] {
                volatile double x = 1.0;
                while (!done_.load(std::memory_order_relaxed)) {
                    x = x * 1.0000001 + 1e-9;
                }
            });
        }
    }
    ~Load() {
        done_.store(true);
        for (std::thread& t : threads_) {
            t.join();
        }
    }
    Load(const Load&) = delete;
    Load& operator=(const Load&) = delete;

private:
    std::atomic<bool> done_{false};
    std::vector<std::thread> threads_;
};

// A drawn permutation of the reaches, one per thread and trial.
std::vector<std::size_t> orderOf(std::size_t count, std::uint64_t seed) {
    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) {
        order[i] = i;
    }
    for (std::size_t i = count; i > 1; --i) {
        seed = terrain::noise::deriveSeed(seed, i);
        std::swap(order[i - 1], order[static_cast<std::size_t>(seed % i)]);
    }
    return order;
}

struct Trial {
    double latency = 0.0;   // ms from the raise to the last thread out
    double remaining = 0.0; // ms the slowest course in progress then takes to be finished
    int stopped = 0;        // threads that left by WorkStopped
};

// kWorkers threads computing every course of a fresh hydrology, each in its order, their flag
// raised after delay ms.
Trial stopTrial(Hydrology& h, int delay, std::uint64_t seed) {
    std::atomic<bool> stop{false};
    std::array<std::size_t, kWorkers> current{};
    std::array<Clock::time_point, kWorkers> out{};
    std::array<bool, kWorkers> left{};
    std::vector<std::thread> workers;
    for (int t = 0; t < kWorkers; ++t) {
        workers.emplace_back([&, t] {
            const WorkStopScope scope(&stop);
            try {
                for (const std::size_t r : orderOf(h.reachCount(), seed + 7 * t)) {
                    current[static_cast<std::size_t>(t)] = r;
                    (void)h.reach(r);
                }
            } catch (const WorkStopped&) {
                left[static_cast<std::size_t>(t)] = true;
            }
            out[static_cast<std::size_t>(t)] = Clock::now();
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    const Clock::time_point raised = Clock::now();
    stop.store(true);
    for (std::thread& t : workers) {
        t.join();
    }
    Trial trial;
    for (int t = 0; t < kWorkers; ++t) {
        trial.latency = std::max(trial.latency, msBetween(raised, out[static_cast<std::size_t>(t)]));
        trial.stopped += left[static_cast<std::size_t>(t)] ? 1 : 0;
    }
    // The end of the courses in progress, as the threads would have made them (one at a time
    // here: the slowest bounds the wait of a join).
    for (int t = 0; t < kWorkers; ++t) {
        const Clock::time_point start = Clock::now();
        (void)h.reach(current[static_cast<std::size_t>(t)]);
        trial.remaining = std::max(trial.remaining, msBetween(start, Clock::now()));
    }
    return trial;
}

std::uint64_t sampleSum(const Hydrology& h) {
    hydrology_sample::Checksum sum;
    for (const std::size_t r : hydrology_sample::sampleReaches(h)) {
        hydrology_sample::addReach(sum, h.reach(r));
    }
    return sum.value();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

struct World {
    terrain::Heightfield relief{map_world::kSeed, terrain::HeightfieldParams{},
                                golden::kSmallPlanet};
    std::shared_ptr<const terrain::PlanetLevel> planet = map_world::smallPlanet();

    [[nodiscard]] std::unique_ptr<Hydrology> fresh() const {
        return std::make_unique<Hydrology>(map_world::kSeed, relief, planet);
    }
};

void testStopsUnderLoad(const World& w) {
    const Load load;
    std::vector<double> latencies;
    double worst = 0.0;
    for (int k = 0; k < kTrials; ++k) {
        const auto h = w.fresh();
        const int delay = 40 + static_cast<int>(terrain::noise::deriveSeed(39, k) % 360);
        const Trial t = stopTrial(*h, delay, 1000 + k);
        latencies.push_back(t.latency);
        worst = std::max(worst, t.latency / std::max(t.remaining, 1e-3));
        std::printf("stop after %d ms: %d of %d threads by WorkStopped, out in %.2f ms; the "
                    "courses in progress finished in %.1f ms\n",
                    delay, t.stopped, kWorkers, t.latency, t.remaining);
        TEST_CHECK(t.stopped == kWorkers);
        if (k == 0) {
            TEST_CHECK(sampleSum(*h) == kSampleReaches); // nothing half made
        }
    }
    std::printf("median %.2f ms, longest %.2f ms, at most %.3f of the end of the courses\n",
                median(latencies), *std::max_element(latencies.begin(), latencies.end()), worst);
    TEST_CHECK(median(latencies) <= 100.0);
    TEST_CHECK(worst < 0.25);
}

// A thread waiting for the course another one makes (no flag) leaves once its flag is raised;
// the other one finishes it, the same bits. The course waited for is the longest to make of the
// sample (with what it needs first).
void testWaiter(const World& w) {
    const auto reference = w.fresh();
    std::size_t r = 0;
    double longest = 0.0;
    for (const std::size_t k : hydrology_sample::sampleReaches(*reference)) {
        const Clock::time_point start = Clock::now();
        (void)reference->reach(k);
        if (msBetween(start, Clock::now()) > longest) {
            longest = msBetween(start, Clock::now());
            r = k;
        }
    }
    hydrology_sample::Checksum expected;
    hydrology_sample::addReach(expected, reference->reach(r));
    std::printf("the course waited for: reach %zu, %.1f ms to make\n", r, longest);
    const Load load;
    std::vector<double> latencies;
    for (int k = 0; k < 5; ++k) {
        const auto h = w.fresh();
        std::atomic<bool> stop{false};
        bool left = false;
        Clock::time_point out;
        Clock::time_point made;
        hydrology_sample::Checksum got;
        std::thread maker([&] {
            hydrology_sample::addReach(got, h->reach(r));
            made = Clock::now();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::thread waiter([&] {
            const WorkStopScope scope(&stop);
            try {
                (void)h->reach(r);
            } catch (const WorkStopped&) {
                left = true;
            }
            out = Clock::now();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
        const Clock::time_point raised = Clock::now();
        stop.store(true);
        waiter.join();
        maker.join();
        const double latency = msBetween(raised, out);
        const double remaining = msBetween(raised, made);
        latencies.push_back(latency);
        std::printf("waiting thread out in %.2f ms; the course made %.1f ms after the stop\n",
                    latency, remaining);
        TEST_CHECK(left);
        TEST_CHECK(remaining <= 0.0 || latency < 0.25 * remaining);
        TEST_CHECK(got.value() == expected.value());
    }
    TEST_CHECK(median(latencies) <= 100.0);
}

// One thread stopped, the other one going on: its courses those of the sample.
void testOthersGoOn(const World& w) {
    const auto h = w.fresh();
    std::atomic<bool> stopA{false};
    std::atomic<bool> stopB{false};
    bool leftA = false;
    bool leftB = false;
    std::uint64_t sumB = 0;
    std::thread a([&] {
        const WorkStopScope scope(&stopA);
        try {
            for (const std::size_t r : orderOf(h->reachCount(), 5)) {
                (void)h->reach(r);
            }
        } catch (const WorkStopped&) {
            leftA = true;
        }
    });
    std::thread b([&] {
        const WorkStopScope scope(&stopB);
        try {
            sumB = sampleSum(*h);
        } catch (const WorkStopped&) {
            leftB = true;
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stopA.store(true);
    a.join();
    b.join();
    TEST_CHECK(leftA);
    TEST_CHECK(!leftB);
    TEST_CHECK(sumB == kSampleReaches);
}

} // namespace

int main() {
    const World world;
    testStopsUnderLoad(world);
    testWaiter(world);
    testOthersGoOn(world);
    return test::result();
}
