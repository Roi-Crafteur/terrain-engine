// The hydrology of the small world (phase 11B): the same bits whatever the threads and the order
// of the points; the water always going down along the courses, which meet at the points of the
// graph; the lakes flat at their levels; the falls at breaks of slope only, wet down their faces;
// no step across the valleys; a golden checksum.
#include "terrain/generation/climate.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include "golden_support.hpp"
#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

using terrain::HeightSample;
using terrain::Heightfield;
using terrain::Hydrology;
using terrain::RiverReach;

struct World {
    Heightfield relief;
    std::shared_ptr<const terrain::PlanetLevel> planet;
    std::shared_ptr<const Hydrology> hydrology;
    Heightfield field;
};

World makeWorld() {
    const Heightfield relief(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                             golden::kSmallPlanet);
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(2)),
        golden::smallPlanetParams(2));
    auto hydrology =
        std::make_shared<const Hydrology>(terrain::kDefaultWorldSeed, relief, planet);
    return {relief, planet, hydrology, relief.withHydrology(hydrology)};
}

// Points around the rivers: a lattice of 250 m over 12 km boxes around points of the graph.
std::vector<std::array<double, 2>> pointsNearRivers(const World& w, std::size_t boxes) {
    std::vector<std::array<double, 2>> points;
    const auto& segments = w.planet->rivers().segments;
    for (std::size_t s = 0; s < segments.size() && boxes > 0; s += 11, --boxes) {
        const terrain::RiverVertex& v = segments[s].vertices[segments[s].vertices.size() / 2];
        for (int j = -24; j <= 24; ++j) {
            for (int i = -24; i <= 24; ++i) {
                points.push_back({v.x + 250.0 * i + 0.37, v.z + 250.0 * j + 0.61});
            }
        }
    }
    return points;
}

bool same(const HeightSample& a, const HeightSample& b) {
    return std::bit_cast<std::uint32_t>(a.height) == std::bit_cast<std::uint32_t>(b.height) &&
           a.water == b.water &&
           std::bit_cast<std::uint32_t>(a.waterLevel) == std::bit_cast<std::uint32_t>(b.waterLevel);
}

// The same bits on 4 threads in reverse order and on a new hydrology, as on one thread.
void testDeterminism(const World& w, const std::vector<std::array<double, 2>>& points) {
    std::vector<HeightSample> one(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        one[i] = w.field.sampleAt(points[i][0], points[i][1]);
    }
    const World fresh = makeWorld();
    std::vector<HeightSample> four(points.size());
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            for (std::size_t k = t; k < points.size(); k += 4) {
                const std::size_t i = points.size() - 1 - k;
                four[i] = fresh.field.sampleAt(points[i][0], points[i][1]);
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    bool equal = true;
    std::size_t carved = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        equal = equal && same(one[i], four[i]);
        carved += w.relief.sampleAt(points[i][0], points[i][1]).height != one[i].height ? 1u : 0u;
    }
    TEST_CHECK(equal);
    TEST_CHECK(carved > points.size() / 50);
    std::printf("%zu points near the rivers, %zu carved\n", points.size(), carved);
}

// Every course: its water never rises, never stands above the levels of the graph at its ends
// (but at a confluence), and the next reach on its segment starts at the water where it ends; the falls stand where
// the course is gentle around them.
void testCourses(const World& w) {
    const Hydrology& h = *w.hydrology;
    bool down = true;
    bool ends = true;
    bool meets = true;
    bool breaks = true;
    std::size_t falls = 0;
    std::size_t joints = 0;
    for (std::size_t r = 0; r < h.reachCount(); r += 27) {
        const RiverReach reach = h.reach(r);
        for (std::size_t i = 1; i < reach.points.size(); ++i) {
            down = down && !(reach.points[i].level > reach.points[i - 1].level);
        }
        const terrain::RiverSegment& s =
            w.planet->rivers().segments[static_cast<std::size_t>(reach.segment)];
        const auto v = static_cast<std::size_t>(reach.vertex + 1);
        // A tributary ends at the water of the river it joins.
        const bool joins = v + 1 == s.vertices.size() && s.mouth == terrain::RiverMouth::Confluence;
        ends = ends && (joins || !(reach.points.back().level > s.vertices[v].elevation));
        if (reach.vertex >= 0) {
            ends = ends && !(reach.points.front().level >
                             s.vertices[static_cast<std::size_t>(reach.vertex)].elevation);
        }
        const terrain::RiverPoint& last = reach.points.back();
        for (const std::size_t k : h.reachesNear(last.x, last.z, 1.0)) {
            const RiverReach after = h.reach(k);
            if (after.segment == reach.segment && after.vertex == reach.vertex + 1) {
                meets = meets && after.points.front().level == last.level;
                ++joints;
            }
        }
        for (const terrain::RiverFall& f : reach.falls) {
            ++falls;
            const double drop = static_cast<double>(f.top - f.bottom);
            breaks = breaks && drop > 4.0;
        }
    }
    TEST_CHECK(down && ends && meets && breaks);
    TEST_CHECK(joints > 0);
    std::printf("%zu reaches, %zu falls on one in 27 of them, %zu joints checked\n",
                h.reachCount(), falls, joints);
}

// Across a valley, every meter, and where the ground changes most between two points, every 10
// cm: steep faces of rock (the walls of the gorges) but no step, the change ten times finer.
void testCrossSections(const World& w) {
    const Hydrology& h = *w.hydrology;
    double steepest = 0.0;
    double finest = 0.0;
    for (std::size_t r = 0; r < h.reachCount(); r += 97) {
        const RiverReach reach = h.reach(r);
        const terrain::RiverPoint& p = reach.points[reach.points.size() / 2];
        const terrain::RiverPoint& q = reach.points[reach.points.size() / 2 + 1];
        const double length = std::max(1.0, std::hypot(q.x - p.x, q.z - p.z));
        const double ux = -(q.z - p.z) / length;
        const double uz = (q.x - p.x) / length;
        double worst = 0.0;
        int at = 0;
        float before = w.field.heightAt(p.x - 1500.0 * ux, p.z - 1500.0 * uz);
        for (int k = -1499; k <= 1500; ++k) {
            const float h1 = w.field.heightAt(p.x + k * ux, p.z + k * uz);
            if (std::abs(h1 - before) > worst) {
                worst = std::abs(h1 - before);
                at = k;
            }
            before = h1;
        }
        double fine = 0.0;
        float last = w.field.heightAt(p.x + (at - 1) * ux, p.z + (at - 1) * uz);
        for (int k = 1; k <= 10; ++k) {
            const double d = at - 1 + 0.1 * k;
            const float h1 = w.field.heightAt(p.x + d * ux, p.z + d * uz);
            fine = std::max(fine, static_cast<double>(std::abs(h1 - last)));
            last = h1;
        }
        steepest = std::max(steepest, worst);
        finest = std::max(finest, fine / std::max(worst, 1.0));
    }
    TEST_CHECK(finest < 0.35);
    std::printf("across the valleys: at most %.2f m between points 1 m apart, the finest step at "
                "most %.2f of it\n",
                steepest, finest);
}

// Down the face of a fall the channel is wet from bank to bank: at a quarter, half and three
// quarters of the drop of the falls of one reach in 3, every meter across the inner 70 % of its
// channel. The segments below on the same face reach up it beyond their start and hold about the
// same ground: they must not leave it dry.
void testFallFaces(const World& w) {
    const Hydrology& h = *w.hydrology;
    std::size_t faces = 0;
    std::size_t points = 0;
    std::size_t dry = 0;
    for (std::size_t r = 0; r < h.reachCount(); r += 3) {
        const RiverReach reach = h.reach(r);
        for (const terrain::RiverFall& f : reach.falls) {
            ++faces;
            for (const float share : {0.25f, 0.5f, 0.75f}) {
                const float level = f.top - share * (f.top - f.bottom);
                std::size_t i = 1;
                while (i < reach.points.size() && !(reach.points[i].level < level)) {
                    ++i;
                }
                if (i == reach.points.size()) {
                    continue;
                }
                const terrain::RiverPoint& a = reach.points[i - 1];
                const terrain::RiverPoint& b = reach.points[i];
                const double t = (a.level - level) / (a.level - b.level);
                const double length = std::max(1e-6, std::hypot(b.x - a.x, b.z - a.z));
                const double half = 0.35 * (a.width + t * (b.width - a.width));
                for (double d = -half; d <= half; d += 1.0) {
                    const double x = a.x + t * (b.x - a.x) - d * (b.z - a.z) / length;
                    const double z = a.z + t * (b.z - a.z) + d * (b.x - a.x) / length;
                    ++points;
                    dry += w.field.sampleAt(x, z).water == terrain::WaterKind::None ? 1 : 0;
                }
            }
        }
    }
    TEST_CHECK(faces > 0 && points > 0);
    TEST_CHECK(dry == 0);
    std::printf("down %zu faces of falls, %zu points across their channels, %zu dry\n", faces,
                points, dry);
}

// The lakes are flat at the level of their lake: 16 km around the lowest cells of a few lakes,
// every 125 m (outside the corridors of the rivers, mountain_freeze_test keeps every bit of the
// relief).
void testLakes(const World& w) {
    std::vector<std::array<double, 2>> centers;
    const terrain::PlanetGrid& g = w.planet->grid();
    for (std::size_t l = 0; l < w.planet->lakes().size() && centers.size() < 3; l += 5) {
        const terrain::PlanetLake& lake = w.planet->lakes()[l];
        if (!lake.playa) {
            centers.push_back({g.centerX(g.column(lake.pit)), g.centerZ(g.row(lake.pit))});
        }
    }
    std::size_t lake = 0;
    bool flat = true;
    for (const auto& c : centers) {
        for (int j = -64; j < 64; ++j) {
            for (int i = -64; i < 64; ++i) {
                const double x = c[0] + 125.0 * i + 0.3;
                const double z = c[1] + 125.0 * j + 0.7;
                const HeightSample s = w.field.sampleAt(x, z);
                if (s.water == terrain::WaterKind::Lake ||
                    s.water == terrain::WaterKind::SaltLake) {
                    ++lake;
                    // Over the relief the lakes were found over (without its coasts, which keep
                    // off them: Hydrology::clearance).
                    const terrain::LakeWater l =
                        w.hydrology->lakeAt(x, z, w.hydrology->relief().heightAt(x, z));
                    flat = flat && l.lake >= 0 && s.waterLevel == l.level;
                }
            }
        }
    }
    TEST_CHECK(flat && lake > 0);
    std::printf("%zu points under lakes, each at the level of its lake\n", lake);
}

// The humidity of the planet level: P / (P + PET) of the cell at its center (PET 300 mm/yr at
// least), within [0, 1]
// everywhere, and what the climate of the world reads with it (planetHumidity), the same bits.
void testHumidity(const World& w) {
    const terrain::PlanetLevel& level = *w.planet;
    const terrain::PlanetGrid& grid = level.grid();
    bool centers = true;
    bool bounded = true;
    bool read = true;
    double dry = 1.0;
    double wet = 0.0;
    const terrain::ClimateField own(terrain::kDefaultWorldSeed, {}, golden::kSmallPlanet);
    const terrain::ClimateField planet(terrain::kDefaultWorldSeed, {}, golden::kSmallPlanet,
                                       terrain::planetHumidity(w.hydrology));
    for (std::size_t cell = 0; cell < grid.cells(); cell += 97) {
        const double x = grid.centerX(grid.column(cell));
        const double z = grid.centerZ(grid.row(cell));
        const double p = level.precipitation(cell);
        const double pet = std::max(terrain::planet::potentialEvapotranspiration(
                                        level.temperature(cell), level.params().climate),
                                    300.0);
        const float h = level.humidityAt(x, z);
        centers = centers && std::abs(h - static_cast<float>(p / (p + pet))) < 1e-5f;
        bounded = bounded && h >= 0.0f && h <= 1.0f;
        read = read && planet.humidityAt(x, z, 0.5f) == h;
        dry = std::min(dry, static_cast<double>(h));
        wet = std::max(wet, static_cast<double>(h));
    }
    TEST_CHECK(centers);
    TEST_CHECK(bounded);
    TEST_CHECK(read);
    // The planet ranges from deserts to wet lands, unlike the noise of the climate alone.
    TEST_CHECK(dry < 0.2 && wet > 0.6);
    TEST_CHECK(own.humidityAt(0.0, 0.0, 0.5f) != planet.humidityAt(0.0, 0.0, 0.5f));
    TEST_CHECK(terrain::planetHumidity(nullptr) == nullptr);
    std::printf("humidity of the planet level from %.2f to %.2f\n", dry, wet);
}

void testGolden(const World& w, const std::vector<std::array<double, 2>>& points) {
    golden::Checksum sum;
    for (const auto& p : points) {
        const HeightSample s = w.field.sampleAt(p[0], p[1]);
        sum.add(s.height);
        sum.add(static_cast<std::uint64_t>(s.water), 1);
        sum.add(s.waterLevel);
    }
    // The coasts (agent 33) change 1,702 of its 19,208 points, all in their band, out of the
    // corridors of the rivers (0x3F29B8BF9DA8A0BB without them).
    constexpr std::uint64_t kExpected = 0x08403A22B852F731ULL;
    std::printf("{\"hydrology of the small world\", 0x%016llXULL}%s\n",
                static_cast<unsigned long long>(sum.value()),
                sum.value() == kExpected ? "" : "  (differs)");
    TEST_CHECK(sum.value() == kExpected);
}

} // namespace

int main() {
    const World w = makeWorld();
    const std::vector<std::array<double, 2>> points = pointsNearRivers(w, 8);
    testDeterminism(w, points);
    testCourses(w);
    testCrossSections(w);
    testFallFaces(w);
    testLakes(w);
    testHumidity(w);
    testGolden(w, points);
    return test::result();
}
