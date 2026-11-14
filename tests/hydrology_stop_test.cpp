// The computations of the hydrology stopped from another thread (agent 39, work_stop.hpp), under
// load (as many spinning threads as working ones), on the small world. Two tests of this
// executable (hydrology_stop_tests.cmake), each alone on the machine (serial_tests.cmake):
// - hydrology_stop_test (argument stops):
//   - 4 threads computing the long courses of the sample (100 ms or more each to make), their
//     flag raised at drawn instants, in the middle of the first: every one leaves by WorkStopped,
//     none finishes the course it was making; the longest time to their last exit is a quarter
//     of the time the courses in progress then take to be finished, at most (measured in the
//     same trial, on the same caches, as a join would wait for them: the four finished at once
//     under the same load);
//   - a stopped thread leaves nothing half made: the courses then computed on the hydrology of
//     the first trial are those of a hydrology never stopped (the bits of hydrology_bits_test);
// - hydrology_wait_test (argument waits):
//   - a thread waiting for a course another thread makes leaves as soon (woken by the raise of
//     its flag, never by the end of a step of its wait: lazy_wake_test), under a quarter of what
//     the course still takes, 250 ms or more there;
//   - a thread stopped leaves the others computing, their results unchanged.
// No argument: both. The medians have a goal of 100 ms, printed (time goal) and checked only by
// hydrology_stop_test_bench (TERRAIN_TIME_GOALS, agent 42): a loaded machine may miss it.
#include "hydrology_sample_support.hpp"

#include "terrain/work_stop.hpp"
#include "time_goal_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using terrain::Hydrology;
using terrain::raiseWorkStop;
using terrain::WorkStopped;
using terrain::WorkStopScope;

constexpr int kWorkers = 4;
constexpr int kTrials = 10;
// A course long to make: what the threads stopped under load compute.
constexpr double kLongCourse = 100.0; // ms
// The course a thread waits for in testWaiter: among the longest of the small world to make alone
// on a fresh hydrology (310 to 380 ms here; every reach timed once by agent 39).
constexpr std::size_t kWaitCourse = 2174;
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

// The reaches of the sample and the time each takes to make (with what it needs first) on a
// fresh hydrology, in the order of the sample.
struct Timed {
    std::size_t reach = 0;
    double ms = 0.0;
};

// kWorkers threads computing the long courses of a fresh hydrology (thread t from the t-th, in
// turn), then every other course, their flag raised after delay ms.
Trial stopTrial(Hydrology& h, const std::vector<std::size_t>& longs, int delay,
                std::uint64_t seed) {
    std::atomic<bool> stop{false};
    std::array<std::size_t, kWorkers> current{};
    std::array<Clock::time_point, kWorkers> out{};
    std::array<bool, kWorkers> left{};
    std::vector<std::thread> workers;
    for (int t = 0; t < kWorkers; ++t) {
        workers.emplace_back([&, t] {
            const WorkStopScope scope(&stop);
            try {
                const auto first = static_cast<std::size_t>(t);
                for (std::size_t k = 0; k < longs.size(); ++k) {
                    current[first] = longs[(first + k) % longs.size()];
                    (void)h.reach(current[first]);
                }
                const std::uint64_t own = seed + 7 * static_cast<std::uint64_t>(t);
                for (const std::size_t r : orderOf(h.reachCount(), own)) {
                    current[first] = r;
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
    raiseWorkStop(stop);
    for (std::thread& t : workers) {
        t.join();
    }
    Trial trial;
    for (int t = 0; t < kWorkers; ++t) {
        const double ms = msBetween(raised, out[static_cast<std::size_t>(t)]);
        trial.latency = std::max(trial.latency, ms);
        trial.stopped += left[static_cast<std::size_t>(t)] ? 1 : 0;
    }
    // The end of the courses in progress, as a join would wait for it: each finished by its own
    // thread, the four at once, under the same load as the stop; until the last one.
    std::vector<std::thread> finishers;
    const Clock::time_point start = Clock::now();
    for (int t = 0; t < kWorkers; ++t) {
        finishers.emplace_back(
            [&h, r = current[static_cast<std::size_t>(t)]] { (void)h.reach(r); });
    }
    for (std::thread& f : finishers) {
        f.join();
    }
    trial.remaining = msBetween(start, Clock::now());
    return trial;
}

std::uint64_t sampleSum(const Hydrology& h) {
    hydrology_sample::Checksum sum;
    for (const std::size_t r : hydrology_sample::sampleReaches(h)) {
        hydrology_sample::addReach(sum, h.reach(r));
    }
    return sum.value();
}

// The lines of each trial, printed only when a check failed since `from`: ctest keeps 1 KB of the
// output of a test that passes, its summary lines.
void printOnFailure(int from, const std::vector<std::string>& lines) {
    if (test::failures > from) {
        for (const std::string& line : lines) {
            std::printf("%s\n", line.c_str());
        }
    }
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

// The courses of the sample, each with the time it takes to make there.
std::vector<Timed> timeSample(const World& w) {
    const auto h = w.fresh();
    std::vector<Timed> out;
    for (const std::size_t r : hydrology_sample::sampleReaches(*h)) {
        const Clock::time_point start = Clock::now();
        (void)h->reach(r);
        out.push_back({r, msBetween(start, Clock::now())});
    }
    return out;
}

// The courses of kLongCourse or more, the longest first; the kWorkers longest on a machine so
// fast that fewer than kWorkers are that long (said).
std::vector<std::size_t> longCourses(std::vector<Timed> timed) {
    std::sort(timed.begin(), timed.end(),
              [](const Timed& a, const Timed& b) { return a.ms > b.ms; });
    std::vector<std::size_t> out;
    for (const Timed& t : timed) {
        if (t.ms >= kLongCourse || out.size() < static_cast<std::size_t>(kWorkers)) {
            out.push_back(t.reach);
        }
    }
    std::printf("%zu courses of the sample to stop in: %.1f to %.1f ms to make\n", out.size(),
                timed[out.size() - 1].ms, timed.front().ms);
    return out;
}

void testStopsUnderLoad(const World& w, const std::vector<Timed>& timed) {
    const std::vector<std::size_t> longs = longCourses(timed);
    std::optional<Load> load;
    load.emplace();
    const int before = test::failures;
    std::vector<std::string> lines;
    std::vector<double> latencies;
    double worst = 0.0;
    std::unique_ptr<Hydrology> first; // read again once the load is gone: nothing half made
    for (int k = 0; k < kTrials; ++k) {
        auto h = w.fresh();
        const auto draw = terrain::noise::deriveSeed(39, static_cast<std::uint64_t>(k));
        const int delay = 10 + static_cast<int>(draw % 80); // in the middle of the first course
        const Trial t = stopTrial(*h, longs, delay, 1000 + static_cast<std::uint64_t>(k));
        latencies.push_back(t.latency);
        worst = std::max(worst, t.latency / std::max(t.remaining, 1e-3));
        std::array<char, 160> line{};
        std::snprintf(line.data(), line.size(),
                      "stop after %d ms: %d of %d threads by WorkStopped, out in %.2f ms; the "
                      "courses in progress finished in %.1f ms",
                      delay, t.stopped, kWorkers, t.latency, t.remaining);
        lines.emplace_back(line.data());
        TEST_CHECK(t.stopped == kWorkers);
        if (k == 0) {
            first = std::move(h); // untouched by the later trials, each on its own hydrology
        }
    }
    load.reset();
    // The courses of the sample on the hydrology of the first trial, as it was left by its stop
    // and the end of its courses in progress: those of a hydrology never stopped.
    TEST_CHECK(sampleSum(*first) == kSampleReaches);
    std::printf("median %.2f ms, longest %.2f ms, at most %.3f of the end of the courses\n",
                median(latencies), *std::max_element(latencies.begin(), latencies.end()), worst);
    time_goal::report("median stop", median(latencies), 100.0, "ms");
    TEST_CHECK(worst < 0.25);
    printOnFailure(before, lines);
}

// A thread waiting for the course another one makes (no flag) leaves once its flag is raised;
// the other one finishes it, the same bits. The course waited for is kWaitCourse, long to make on
// a fresh hydrology: raised 8 ms after it started, its maker still has 250 ms or more to go on
// every runner (16 ticks of Windows), against which the exit of the waiting thread is compared.
// A world without that reach: the longest course of the sample.
void testWaiter(const World& w) {
    const auto reference = w.fresh();
    std::size_t r = kWaitCourse;
    if (r >= reference->reachCount()) {
        const std::vector<Timed> timed = timeSample(w);
        r = std::max_element(timed.begin(), timed.end(), [](const Timed& a, const Timed& b) {
                return a.ms < b.ms;
            })->reach;
    }
    const Clock::time_point start = Clock::now();
    hydrology_sample::Checksum expected;
    hydrology_sample::addReach(expected, reference->reach(r));
    std::printf("the course waited for: reach %zu, %.1f ms to make\n", r,
                msBetween(start, Clock::now()));
    const Load load;
    const int before = test::failures;
    std::vector<std::string> lines;
    std::vector<double> latencies;
    double worst = 0.0;
    double least = 1e9; // ms the course still took after the raise, at least
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
        raiseWorkStop(stop);
        waiter.join();
        maker.join();
        const double latency = msBetween(raised, out);
        const double remaining = msBetween(raised, made);
        latencies.push_back(latency);
        worst = std::max(worst, latency / std::max(remaining, 1e-3));
        least = std::min(least, remaining);
        std::array<char, 160> line{};
        std::snprintf(line.data(), line.size(),
                      "waiting thread out in %.2f ms; the course made %.1f ms after the stop",
                      latency, remaining);
        lines.emplace_back(line.data());
        TEST_CHECK(left);
        TEST_CHECK(remaining <= 0.0 || latency < 0.25 * remaining);
        TEST_CHECK(got.value() == expected.value());
    }
    std::printf("waiting thread out in %.2f ms (median of 5), at most %.3f of the course made "
                "after the stop (%.0f ms at least)\n",
                median(latencies), worst, least);
    time_goal::report("median exit of the waiting thread", median(latencies), 100.0, "ms");
    printOnFailure(before, lines);
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
    raiseWorkStop(stopA);
    a.join();
    b.join();
    TEST_CHECK(leftA);
    TEST_CHECK(!leftB);
    TEST_CHECK(sumB == kSampleReaches);
}

} // namespace

int main(int argc, char** argv) {
    const std::string part = argc > 1 ? argv[1] : "";
    const World world;
    if (part.empty() || part == "stops") {
        testStopsUnderLoad(world, timeSample(world));
    }
    if (part.empty() || part == "waits") {
        testWaiter(world);
        testOthersGoOn(world);
    }
    TEST_CHECK(part.empty() || part == "stops" || part == "waits");
    return time_goal::result(test::result());
}
