// The rivers through the band of the coasts (agent 33), on the small world with its planet level:
// the coasts are left out of the planet level and of the courses of the rivers
// (Heightfield::withoutCoasts), and fade out to the relief without them near the valleys
// (Hydrology::clearance). Checked against the world without the coasts (the same planet level and
// rivers over the relief without them):
// - the clearance: wherever it is above 0 the valleys neither carve nor fill nor hold a bank or a
//   pond there, and no lake lies over the point, whatever the relief;
// - the corridors: every point where the valleys carve, fill or come within the clearance keeps
//   every bit of the world without the coasts, along the last reach of every river ending in the
//   sea and around it (the falls that end in the sea with them);
// - the mouths: down the axis of the last reach of every river ending in the sea, from where it
//   enters the band, no ground stands above its water (its floodplain at most) and the water
//   never rises downstream; its last point lies in the sea.
#include "terrain/generation/planet/planet_build.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include "coast_zone_support.hpp"
#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::HeightSample;
using terrain::RiverReach;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
// The floodplain of a river stands at most kFloodplain meters above its water.
constexpr float kFloodplain = 1.0f;

struct World {
    Heightfield relief;
    std::shared_ptr<const terrain::PlanetLevel> planet;
    std::shared_ptr<const terrain::Hydrology> hydrology;
    Heightfield field; // the world
    Heightfield bare;  // the world without the coasts
};

World makeWorld() {
    const Heightfield relief(kSeed, terrain::HeightfieldParams{}, golden::kSmallPlanet);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threads)),
        golden::smallPlanetParams(threads));
    auto hydrology = std::make_shared<const terrain::Hydrology>(kSeed, relief, planet);
    return {relief, planet, hydrology, relief.withHydrology(hydrology),
            relief.withoutCoasts().withHydrology(hydrology)};
}

bool sameBits(const HeightSample& a, const HeightSample& b) {
    const auto bits = [](const HeightSample& s) {
        return std::array<std::uint32_t, 9>{
            std::bit_cast<std::uint32_t>(s.continentalness),
            std::bit_cast<std::uint32_t>(s.mountainMask), std::bit_cast<std::uint32_t>(s.relief),
            std::bit_cast<std::uint32_t>(s.base), std::bit_cast<std::uint32_t>(s.mountain),
            std::bit_cast<std::uint32_t>(s.detail), std::bit_cast<std::uint32_t>(s.height),
            std::bit_cast<std::uint32_t>(s.waterLevel), static_cast<std::uint32_t>(s.water)};
    };
    return bits(a) == bits(b);
}

// Around every kStride-th point of the course of a reach, a lattice of kSide x kSide points
// kStep meters apart (out of the corridor too); every kAxisStride-th point of its axis; one reach
// in kReachStride.
constexpr std::size_t kStride = 40;
constexpr int kSide = 9;
constexpr double kStep = 190.0;
constexpr std::size_t kAxisStride = 4;
constexpr std::size_t kReachStride = 3;

// The last reach of one river ending in the sea in kReachStride (its vertex the last but one of
// its segment), found among the reaches near its mouth: the courses of the others are never built.
std::vector<std::size_t> mouthReaches(const World& w, std::size_t& rivers) {
    const auto& segments = w.planet->rivers().segments;
    std::vector<std::size_t> out;
    rivers = 0;
    for (std::size_t si = 0; si < segments.size(); ++si) {
        const terrain::RiverSegment& s = segments[si];
        if (s.mouth != terrain::RiverMouth::Sea || s.vertices.size() < 2 ||
            rivers++ % kReachStride != 0) {
            continue;
        }
        const terrain::RiverVertex& v = s.vertices.back();
        for (const std::size_t r : w.hydrology->reachesNear(v.x, v.z, 1'000.0)) {
            const RiverReach reach = w.hydrology->reach(r);
            if (reach.segment == static_cast<std::int32_t>(si) &&
                reach.vertex + 2 == static_cast<std::int32_t>(s.vertices.size()) &&
                !reach.points.empty()) {
                out.push_back(r);
            }
        }
    }
    return out;
}

struct Counts {
    std::size_t points = 0;
    std::size_t clear = 0;    // the clearance above 0
    std::size_t corridor = 0; // the valleys carve, fill or come within the clearance
    std::size_t changed = 0;  // the coasts change the point
};

void checkAround(const World& w, const RiverReach& reach, Counts& n, bool& oracle, bool& kept) {
    const Heightfield& base = w.hydrology->relief();
    for (std::size_t k = 0; k < reach.points.size(); k += kStride) {
        for (int j = 0; j < kSide; ++j) {
            for (int i = 0; i < kSide; ++i) {
                const double x = terrain::wrapCoordinate(
                    reach.points[k].x + kStep * (i - kSide / 2) + 0.3, w.relief.world().x);
                const double z = terrain::wrapCoordinate(
                    reach.points[k].z + kStep * (j - kSide / 2) + 0.7, w.relief.world().z);
                if (!coast_zone::inZone(coast_zone::coastSignal(w.relief, x, z))) {
                    continue;
                }
                ++n.points;
                const double b = static_cast<double>(base.heightAt(x, z));
                const double clear = w.hydrology->clearance(x, z, b);
                const HeightSample world = w.field.sampleAt(x, z);
                const HeightSample bare = w.bare.sampleAt(x, z);
                n.changed += sameBits(world, bare) ? 0 : 1;
                if (clear > 0.0) {
                    ++n.clear;
                    // Whatever the ground: the clearance at a lower ground holds too.
                    for (const double g : {b, b - 0.5 * clear}) {
                        const terrain::RiverCarve c = w.hydrology->riversAt(x, z, g);
                        oracle = oracle && (w.hydrology->clearance(x, z, g) <= 0.0 ||
                                            (!c.carved && c.raise == 0.0 &&
                                             c.bank == -std::numeric_limits<double>::infinity() &&
                                             c.pond == -std::numeric_limits<float>::infinity() &&
                                             w.hydrology->lakeAt(x, z, g).lake < 0));
                    }
                } else {
                    ++n.corridor;
                    kept = kept && sameBits(world, bare);
                }
            }
        }
    }
}

// Down the axis of a reach from where it enters the band: no ground above its water but its
// floodplain, its water never rising, every point in a corridor keeping its bits; its last point
// in the sea.
void checkAxis(const World& w, const RiverReach& reach, std::size_t& points, bool& mouth) {
    float upstream = std::numeric_limits<float>::infinity();
    bool sea = false;
    for (std::size_t k = 0; k < reach.points.size(); ++k) {
        if (k % kAxisStride != 0 && k + 1 != reach.points.size()) {
            continue;
        }
        const terrain::RiverPoint& p = reach.points[k];
        if (!coast_zone::inZone(coast_zone::coastSignal(w.relief, p.x, p.z))) {
            continue;
        }
        ++points;
        const HeightSample s = w.field.sampleAt(p.x, p.z);
        sea = s.water == terrain::WaterKind::Ocean;
        const float level = sea ? terrain::kSeaLevel : p.level;
        mouth = mouth && sameBits(s, w.bare.sampleAt(p.x, p.z)) &&
                s.height <= level + kFloodplain && !(level > upstream);
        upstream = std::min(upstream, level);
    }
    mouth = mouth && sea;
}

void testMouths(const World& w) {
    std::size_t rivers = 0;
    const std::vector<std::size_t> reaches = mouthReaches(w, rivers);
    Counts n;
    bool oracle = true;
    bool kept = true;
    bool mouth = true;
    std::size_t axis = 0;
    std::size_t falls = 0;
    for (const std::size_t r : reaches) {
        const RiverReach reach = w.hydrology->reach(r);
        falls += reach.falls.size();
        checkAround(w, reach, n, oracle, kept);
        checkAxis(w, reach, axis, mouth);
    }
    std::printf("%zu rivers ending in the sea, one in %zu checked (%zu falls on their last "
                "reaches), %zu points of their axes in the band; around them %zu points in the "
                "band: %zu clear of the valleys, %zu in their corridors kept to the bit, %zu "
                "changed by the coasts\n",
                rivers, kReachStride, falls, axis, n.points, n.clear, n.corridor, n.changed);
    TEST_CHECK(rivers > 150 && reaches.size() * kReachStride + kReachStride > rivers);
    TEST_CHECK(oracle);
    TEST_CHECK(kept);
    TEST_CHECK(mouth);
    TEST_CHECK(n.corridor > 0 && n.clear > 0 && n.changed > 0);
}

} // namespace

int main() {
    const World w = makeWorld();
    testMouths(w);
    return test::result();
}
