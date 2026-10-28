// Measures of the GPU far horizon, run by hand (not a test): the HorizonGpuStreamer of the bridge
// around a place, its jobs on a pool of 1 or more threads as Unreal runs them on its tasks. Prints
// the time from the start to the first commit (the levels shown, crests included when they are
// computed in the epoch), to the last crests and to the last detail normals, level by level, then
// the updates of a walk of 2 km along the heading in steps of 8 m: the epoch of each step and the
// time until its crests and normals are all in. The memory: the samples, the detail textures and
// the peak resident size of the process (Linux).
//
//   unreal_horizon_bench <k2_valley|chain_foot> <before|points|after2|after4|epoch4> <threads>
//                        [steps]
//
// before: crests at 2 in the epochs (the horizon before the deferred crests); points: no crest
// (the epoch alone); after2 and after4: crests at 2 or 4 after the epochs, with the normals;
// epoch4: crests at 4 in the epochs. Detail normals at ratio 4 in every configuration.
#include "horizon_gpu_streamer.hpp"

#include "horizon_test_support.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using terrain::unreal::HorizonGpuParams;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::HorizonGpuUpdate;
using Clock = std::chrono::steady_clock;

struct Place {
    const char* name;
    double x;
    double z;
    double headingDegrees;
};

constexpr std::array<Place, 2> kPlaces{{{"k2_valley", 416'857.0, -11'398'966.0, 0.0},
                                        {"chain_foot", 9'618'717.0, -11'025'212.0, 67.5}}};

struct Config {
    const char* name;
    int crestRatio;
    bool deferred;
};

constexpr std::array<Config, 5> kConfigs{{{"before", 2, false},
                                          {"points", 0, false},
                                          {"after2", 2, true},
                                          {"after4", 4, true},
                                          {"epoch4", 4, false}}};

// The jobs of the streamer on `threads` threads, first come first served.
class Pool {
public:
    explicit Pool(int threads) {
        for (int i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { work(); });
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
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    [[nodiscard]] terrain::unreal::JobLauncher launcher() {
        return [this](std::function<void()> job) {
            {
                const std::lock_guard lock(mutex_);
                jobs_.push_back(std::move(job));
            }
            ready_.notify_one();
        };
    }

private:
    void work() {
        std::unique_lock lock(mutex_);
        while (true) {
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

template <typename Params>
concept Deferrable = requires(Params params) { params.deferCrests; };

template <typename Streamer>
concept WithBusy = requires(Streamer& streamer) { streamer.busy(); };

template <typename Params>
void setDeferred(Params& params, bool deferred) {
    if constexpr (Deferrable<Params>) {
        params.deferCrests = deferred;
    } else if (deferred) {
        throw std::invalid_argument("no deferred crests in this build");
    }
}

HorizonGpuParams paramsOf(const Config& config, int threads) {
    HorizonGpuParams params;
    params.maxPiecesInFlight = threads;
    params.normalJobs = threads;
    params.normalRatio = 4;
    params.crestRatio = config.crestRatio;
    setDeferred(params, config.deferred);
    return params;
}

// Whether an epoch or some detail is still to come (the normals carry the crests when they are
// deferred).
template <typename Streamer>
bool busy(Streamer& streamer, bool committed) {
    if constexpr (WithBusy<Streamer>) {
        return !committed || streamer.busy();
    } else {
        return !committed || streamer.normals()->busy();
    }
}

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// When the texels and the normals of each level last arrived, seconds from the start.
struct Arrivals {
    double commit = 0.0;
    std::vector<double> texels = std::vector<double>(terrain::kHorizonMaxLevels, 0.0);
    std::vector<double> normals = std::vector<double>(terrain::kHorizonMaxLevels, 0.0);

    void record(const HorizonGpuUpdate& update, double at) {
        if (update.committed && commit == 0.0) {
            commit = at;
        }
        for (const auto& region : update.regions) {
            texels[static_cast<std::size_t>(region.level)] = at;
        }
        for (const auto& region : update.normalRegions) {
            normals[static_cast<std::size_t>(region.level)] = at;
        }
    }
    [[nodiscard]] double lastTexels() const {
        return *std::max_element(texels.begin(), texels.end());
    }
    [[nodiscard]] double lastNormals() const {
        return *std::max_element(normals.begin(), normals.end());
    }
};

// Updates around the player until its epoch is committed and its detail is in.
Arrivals settle(HorizonGpuStreamer& streamer, const terrain::unreal::UnrealFrame& frame,
                const terrain::unreal::UnrealPoint& player, std::chrono::microseconds poll) {
    const auto start = Clock::now();
    Arrivals arrivals;
    bool committed = false;
    while (true) {
        const HorizonGpuUpdate update = streamer.update(player, frame);
        committed = committed || update.committed;
        arrivals.record(update, secondsSince(start));
        if (!busy(streamer, committed)) {
            return arrivals;
        }
        std::this_thread::sleep_for(poll);
    }
}

std::size_t peakResidentBytes() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            return static_cast<std::size_t>(std::strtoull(line.c_str() + 6, nullptr, 10)) * 1024;
        }
    }
    return 0;
}

void printFirst(const Arrivals& first, int levels) {
    std::printf("  shown %.2f s, crests in %.2f s, normals in %.2f s\n", first.commit,
                first.lastTexels(), first.lastNormals());
    std::printf("  last crests per level (s):");
    for (int n = 0; n < levels; ++n) {
        std::printf(" %.1f", first.texels[static_cast<std::size_t>(n)]);
    }
    std::printf("\n  last normals per level (s):");
    for (int n = 0; n < levels; ++n) {
        std::printf(" %.1f", first.normals[static_cast<std::size_t>(n)]);
    }
    std::printf("\n");
}

// The walk: epoch per step (plan to commit) and time until the detail of the step is in.
void walk(HorizonGpuStreamer& streamer, const terrain::unreal::UnrealFrame& frame,
          const Place& place, int steps) {
    const double turns = place.headingDegrees / 360.0;
    const double dx = terrain::dmath::sinTurns(turns);
    const double dz = -terrain::dmath::cosTurns(turns);
    double total = 0.0;
    double worst = 0.0;
    double epochs = 0.0;
    for (int step = 1; step <= steps; ++step) {
        const terrain::unreal::UnrealPoint player{800.0 * step * dx, 800.0 * step * dz, 0.0};
        const auto start = Clock::now();
        const Arrivals arrivals = settle(streamer, frame, player, std::chrono::microseconds(100));
        const double seconds = secondsSince(start);
        total += seconds;
        worst = std::max(worst, seconds);
        epochs += arrivals.commit;
    }
    std::printf("  walk of %d steps of 8 m: epoch %.2f ms, detail in %.2f ms per step (max "
                "%.1f ms)\n",
                steps, 1e3 * epochs / steps, 1e3 * total / steps, 1e3 * worst);
}

template <typename Item, std::size_t N>
const Item& find(const std::array<Item, N>& items, const char* name) {
    for (const Item& item : items) {
        if (std::strcmp(item.name, name) == 0) {
            return item;
        }
    }
    throw std::invalid_argument(std::string("unknown: ") + name);
}

int run(const Place& place, const Config& config, int threads, int steps) {
    const horizon_test::World world;
    Pool pool(threads);
    const terrain::unreal::UnrealFrame frame{{place.x, 0.0, place.z}};
    HorizonGpuStreamer streamer(world.field, world.ecology, world.field.world(),
                                paramsOf(config, threads), pool.launcher());
    std::printf("%s, %s (crest ratio %d, %s), %d thread%s\n", place.name, config.name,
                config.crestRatio, config.deferred ? "after the epochs" : "in the epochs",
                threads, threads == 1 ? "" : "s");
    const Arrivals first = settle(streamer, frame, {0.0, 0.0, 0.0}, std::chrono::milliseconds(1));
    printFirst(first, streamer.params().levels);
    walk(streamer, frame, place, steps);
    std::printf("  memory: %.1f MB of samples, %.1f MB of detail textures, %.1f MB peak resident\n",
                static_cast<double>(streamer.memoryBytes()) / 1e6,
                static_cast<double>(streamer.normals()->textureBytes()) / 1e6,
                static_cast<double>(peakResidentBytes()) / 1e6);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::fprintf(stderr, "usage: unreal_horizon_bench <k2_valley|chain_foot> "
                             "<before|points|after2|after4|epoch4> <threads> [steps]\n");
        return 2;
    }
    try {
        const int threads = std::atoi(argv[3]);
        const int steps = argc == 5 ? std::atoi(argv[4]) : 250;
        if (threads < 1 || steps < 0) {
            throw std::invalid_argument("threads must be at least 1, steps at least 0");
        }
        return run(find(kPlaces, argv[1]), find(kConfigs, argv[2]), threads, steps);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "unreal_horizon_bench: %s\n", e.what());
        return 1;
    }
}
