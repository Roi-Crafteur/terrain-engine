// The caches of the hydrology bounded in bytes (agent 39, HydrologyParams::cacheBytes): with a
// budget of a few MiB, far under what the sample needs, the courses are dropped and made again,
// the same bits as kept (those of hydrology_bits_test, and the queries at points of a hydrology
// of the default budget); the bytes the cache of the courses holds stay under its share of the
// budget; on 4 threads dropping and reading at once, the same bits as on one.
#include "hydrology_sample_support.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

using hydrology_sample::Checksum;
using terrain::Hydrology;

// The checksum of the sample reaches (hydrology_bits_test, pinned on main).
constexpr std::uint64_t kSampleReaches = 0xF1684975E5044E29ULL;
// A budget far under the courses of the sample (about 25 MB with its points): the caches then
// hold their least (Hydrology::Impl: 4 MiB of courses, 1 of paths, 4 of fine lakes).
constexpr std::size_t kSmallBudget = std::size_t{1} << 20;
constexpr std::size_t kCourseShare = std::size_t{4} << 20;
constexpr int kThreads = 4;

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct World {
    terrain::Heightfield relief{map_world::kSeed, terrain::HeightfieldParams{},
                                golden::kSmallPlanet};
    std::shared_ptr<const terrain::PlanetLevel> planet = map_world::smallPlanet();

    [[nodiscard]] std::unique_ptr<Hydrology> fresh(std::size_t budget) const {
        terrain::HydrologyParams params;
        params.cacheBytes = budget;
        return std::make_unique<Hydrology>(map_world::kSeed, relief, planet, params);
    }
};

std::uint64_t reachSum(const Hydrology& h, std::size_t r) {
    Checksum sum;
    hydrology_sample::addReach(sum, h.reach(r));
    return sum.value();
}

std::uint64_t pointSum(const Hydrology& h, const std::array<double, 2>& p) {
    Checksum sum;
    hydrology_sample::addPoint(sum, h, p[0], p[1]);
    return sum.value();
}

// The points of the sample, one in every kPointStride.
constexpr std::size_t kPointStride = 16;

std::vector<std::array<double, 2>> somePoints(const terrain::PlanetLevel& planet) {
    const auto all = hydrology_sample::samplePoints(planet);
    std::vector<std::array<double, 2>> points;
    for (std::size_t i = 0; i < all.size(); i += kPointStride) {
        points.push_back(all[i]);
    }
    return points;
}

// On a small budget: the courses of the sample, then queries at points (their courses drop
// those of the sample), then the courses of the sample again, made again: the same checksum
// both times, the bytes held under the share of the courses.
void testDroppedAndMadeAgain(const World& w) {
    const auto start = std::chrono::steady_clock::now();
    const auto h = w.fresh(kSmallBudget);
    std::size_t most = 0;
    std::array<std::size_t, 2> made{};
    for (int pass = 0; pass < 2; ++pass) {
        const std::size_t before = h->coursesComputed();
        Checksum sum;
        for (const std::size_t r : hydrology_sample::sampleReaches(*h)) {
            hydrology_sample::addReach(sum, h->reach(r));
            most = std::max(most, h->courseBytes());
        }
        made[static_cast<std::size_t>(pass)] = h->coursesComputed() - before;
        TEST_CHECK(sum.value() == kSampleReaches);
        for (const auto& p : pass == 0 ? somePoints(*w.planet)
                                       : std::vector<std::array<double, 2>>{}) {
            (void)pointSum(*h, p);
            most = std::max(most, h->courseBytes());
        }
    }
    std::printf("budget %zu bytes (courses %zu): the sample made %zu courses, then %zu again "
                "after the points; %zu bytes held at most, %zu now (%.2f s)\n",
                kSmallBudget, kCourseShare, made[0], made[1], most, h->courseBytes(),
                secondsSince(start));
    TEST_CHECK(made[1] > 0); // dropped by the points, then made again
    TEST_CHECK(most <= kCourseShare);
}

// The queries at points (a part of the sample): the same bits on the small budget, on kThreads
// threads each from its own place, as on the default one.
void testPoints(const World& w) {
    const auto start = std::chrono::steady_clock::now();
    const std::vector<std::array<double, 2>> points = somePoints(*w.planet);
    std::vector<std::uint64_t> kept;
    {
        const auto h = w.fresh(terrain::HydrologyParams{}.cacheBytes);
        for (const auto& p : points) {
            kept.push_back(pointSum(*h, p));
        }
    }
    using Sums = std::vector<std::uint64_t>;
    std::vector<Sums> threaded(kThreads, Sums(points.size(), 0));
    {
        const auto h = w.fresh(kSmallBudget);
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t] {
                const auto n = points.size();
                Sums& mine = threaded[static_cast<std::size_t>(t)];
                for (std::size_t k = 0; k < n; ++k) {
                    // Every thread every point, from its own place: they read what others drop.
                    const std::size_t i = (k + static_cast<std::size_t>(t) * n / kThreads) % n;
                    mine[i] = pointSum(*h, points[i]);
                }
            });
        }
        for (std::thread& t : threads) {
            t.join();
        }
        std::printf("%zu points: on the default budget, then on %d threads on the small one, "
                    "%zu courses made by the threads (%.2f s)\n",
                    points.size(), kThreads, h->coursesComputed(), secondsSince(start));
        TEST_CHECK(h->courseBytes() <= kCourseShare);
    }
    for (const Sums& sums : threaded) {
        TEST_CHECK(sums == kept);
    }
}

// A few reaches on kThreads threads at once on the small budget: each the bits of one thread.
void testReachesOnThreads(const World& w) {
    const std::vector<std::size_t> reaches = [&] {
        const auto h = w.fresh(kSmallBudget);
        return hydrology_sample::sampleReaches(*h);
    }();
    std::vector<std::uint64_t> serial;
    {
        const auto h = w.fresh(kSmallBudget);
        for (const std::size_t r : reaches) {
            serial.push_back(reachSum(*h, r));
        }
    }
    const auto h = w.fresh(kSmallBudget);
    using Sums = std::vector<std::uint64_t>;
    std::vector<Sums> got(kThreads, Sums(reaches.size()));
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            const auto n = reaches.size();
            for (std::size_t k = 0; k < n; ++k) {
                const std::size_t i = (k + static_cast<std::size_t>(t) * n / kThreads) % n;
                got[static_cast<std::size_t>(t)][i] = reachSum(*h, reaches[i]);
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    for (const Sums& g : got) {
        TEST_CHECK(g == serial);
    }
}

} // namespace

int main() {
    const World world;
    testDroppedAndMadeAgain(world);
    testPoints(world);
    testReachesOnThreads(world);
    return test::result();
}
