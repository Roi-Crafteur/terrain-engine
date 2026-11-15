// The hydrology read from several threads at once (agent 39): the same bits on 1 and 4 threads,
// each thread in its own order, for the courses (reach, reachesNear) and the queries at points
// (riversAt, clearanceAt, floorDistance, lakeAt), on a fresh hydrology each time; and the same
// bits again when the 4 threads are stopped in the middle (WorkStopped) and the work then done
// to its end: a stop leaves nothing half made in the caches.
#include "hydrology_sample_support.hpp"

#include "terrain/work_stop.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

using hydrology_sample::Checksum;
using terrain::Hydrology;

constexpr int kThreads = 4;

// One item of the sample: a reach (its course) or a point (its queries and the reaches near it).
struct Item {
    bool reach = false;
    std::size_t index = 0;
    double x = 0.0;
    double z = 0.0;
};

std::vector<Item> items(const terrain::PlanetLevel& planet, const Hydrology& h) {
    std::vector<Item> out;
    for (const std::size_t r : hydrology_sample::sampleReaches(h)) {
        out.push_back({true, r, 0.0, 0.0});
    }
    const auto points = hydrology_sample::samplePoints(planet);
    for (std::size_t i = 0; i < points.size(); i += 6) {
        out.push_back({false, 0, points[i][0], points[i][1]});
    }
    return out;
}

std::uint64_t sumOf(const Hydrology& h, const Item& item) {
    Checksum sum;
    if (item.reach) {
        hydrology_sample::addReach(sum, h.reach(item.index));
        return sum.value();
    }
    hydrology_sample::addPoint(sum, h, item.x, item.z);
    for (const std::size_t r : h.reachesNear(item.x, item.z, 3'000.0)) {
        sum.add(static_cast<std::uint64_t>(r));
    }
    return sum.value();
}

// The items in the order of thread t of kThreads: t first, then every kThreads-th, the odd
// threads backward.
std::vector<std::size_t> orderOf(int t, std::size_t count) {
    std::vector<std::size_t> order;
    for (std::size_t k = static_cast<std::size_t>(t); k < count; k += kThreads) {
        order.push_back(k);
    }
    if (t % 2 == 1) {
        std::reverse(order.begin(), order.end());
    }
    return order;
}

// The sums of the items not yet known (0) on kThreads threads, stopped by stop if given.
void fill(const Hydrology& h, const std::vector<Item>& all, std::vector<std::uint64_t>& sums,
          const std::atomic<bool>* stop, std::atomic<int>& stopped) {
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            const terrain::WorkStopScope scope(stop);
            try {
                for (const std::size_t k : orderOf(t, all.size())) {
                    if (sums[k] == 0) {
                        sums[k] = sumOf(h, all[k]);
                    }
                }
            } catch (const terrain::WorkStopped&) {
                stopped.fetch_add(1);
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
}

} // namespace

int main() {
    const terrain::Heightfield relief(map_world::kSeed, terrain::HeightfieldParams{},
                                      golden::kSmallPlanet);
    const std::shared_ptr<const terrain::PlanetLevel> planet = map_world::smallPlanet();
    const auto fresh = [&] {
        return std::make_unique<Hydrology>(map_world::kSeed, relief, planet);
    };

    auto start = std::chrono::steady_clock::now();
    const auto one = fresh();
    const std::vector<Item> all = items(*planet, *one);
    std::vector<std::uint64_t> serial;
    for (const Item& item : all) {
        serial.push_back(sumOf(*one, item));
    }
    const auto seconds = [](auto from) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
    };
    std::printf("%zu items on one thread: %.2f s\n", all.size(), seconds(start));

    start = std::chrono::steady_clock::now();
    std::atomic<int> stopped{0};
    {
        const auto h = fresh();
        std::vector<std::uint64_t> sums(all.size(), 0);
        fill(*h, all, sums, nullptr, stopped);
        TEST_CHECK(sums == serial);
    }
    std::printf("on %d threads: %.2f s\n", kThreads, seconds(start));

    start = std::chrono::steady_clock::now();
    {
        const auto h = fresh();
        std::vector<std::uint64_t> sums(all.size(), 0);
        std::atomic<bool> stop{false};
        std::thread raiser([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            terrain::raiseWorkStop(stop);
        });
        fill(*h, all, sums, &stop, stopped);
        raiser.join();
        const std::size_t before = h->coursesComputed();
        fill(*h, all, sums, nullptr, stopped); // the rest, on the same caches
        std::printf("stopped: %d threads, %zu courses then, %zu at the end, %.2f s\n",
                    stopped.load(), before, h->coursesComputed(), seconds(start));
        TEST_CHECK(stopped.load() == kThreads);
        TEST_CHECK(sums == serial);
    }
    return test::result();
}
