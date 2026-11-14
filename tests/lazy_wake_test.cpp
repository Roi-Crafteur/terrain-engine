// The waking of the caches of the hydrology (agent 39, water/hydrology_lazy.hpp): a thread waiting
// in a cache for an object another thread makes leaves (WorkStopped) once its flag is raised by
// raiseWorkStop, woken at once by the cache (WorkStopWaking), never by the end of a step of its
// wait. Here the step never ends (kLazyForever) and the maker is held at a gate: only that waking
// lets the waiting thread out. Without it, the thread is still waiting after kNet (a net, not a
// measure: the waking takes well under a millisecond) and the test says so.
#include "hydrology_lazy.hpp"
#include "test_assert.hpp"

#include "terrain/work_stop.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;
using terrain::raiseWorkStop;
using terrain::WorkStopped;
using terrain::WorkStopScope;
using terrain::water_detail::kLazyForever;
using terrain::water_detail::Lazy;

constexpr std::chrono::seconds kNet{30};
// The time given to the waiting thread to enter its wait once it asks (it needs microseconds).
constexpr std::chrono::milliseconds kSettle{100};
constexpr int kMade = 7;

// The maker waits there until it opens.
class Gate {
public:
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        opened_.wait(lock, [this] { return open_; });
    }
    void open() {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            open_ = true;
        }
        opened_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable opened_;
    bool open_ = false;
};

void testWokenByTheRaise() {
    Lazy<int> cache(kLazyForever);
    cache.resize(1);
    Gate gate;
    std::atomic<bool> making{false};
    std::thread maker([&] {
        (void)cache.get(0, [&] {
            making = true;
            gate.wait();
            return kMade;
        });
    });
    while (!making) {
        std::this_thread::yield();
    }
    std::atomic<bool> stop{false};
    std::atomic<bool> asking{false};
    bool left = false;
    std::promise<void> out;
    std::future<void> outFuture = out.get_future();
    std::thread waiter([&] {
        const WorkStopScope scope(&stop);
        asking = true;
        try {
            (void)cache.get(0, [] { return -1; }); // made by the maker, never by this thread
        } catch (const WorkStopped&) {
            left = true;
        }
        out.set_value();
    });
    while (!asking) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(kSettle);
    const Clock::time_point raised = Clock::now();
    raiseWorkStop(stop);
    const bool woken = outFuture.wait_for(kNet) == std::future_status::ready;
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - raised).count();
    if (woken) {
        std::printf("the waiting thread out %.3f ms after the raise, the maker still held\n", ms);
    } else {
        std::fprintf(stderr, "the waiting thread was not woken by raiseWorkStop\n");
    }
    gate.open(); // the maker publishes: a thread never woken then gets the object and leaves
    maker.join();
    waiter.join();
    TEST_CHECK(woken);
    TEST_CHECK(left);
    TEST_CHECK(*cache.get(0, [] { return -1; }) == kMade); // published whole by the maker
}

} // namespace

int main() {
    testWokenByTheRaise();
    return test::result();
}
