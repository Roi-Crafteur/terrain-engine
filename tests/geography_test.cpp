// The planetary geography (phase 11R): built to the same bits on 1 and 4 threads and shared
// between Heightfields; periodic to the bit at every image of a point, the corner of the world
// included; the ground below the sea exactly where the coast signal is negative, mountains only
// on land and within their mask; a map whose regional elevation drains (no closed depression at
// its scale); the share of land; mountain belts, high plateaus, island arcs, trenches and
// mid-ocean ridges; the origin on a coast; small worlds; memory; invalid parameters.
#include "terrain/generation/geography.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_params.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using bit_exact::Point;
using bit_exact::same;
using terrain::Geography;
using terrain::GeographyParams;
using terrain::Heightfield;
using terrain::HeightfieldParams;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

HeightfieldParams geographyParams() {
    HeightfieldParams params;
    params.geography.enabled = true;
    return params;
}

GeographyParams enabled() {
    GeographyParams params;
    params.enabled = true;
    return params;
}

void testDeterminismAndSharing() {
    const Geography one(kSeed, enabled(), WorldSize{}, 1);
    const Geography four(kSeed, enabled(), WorldSize{}, 4);
    TEST_CHECK(one.checksum() == four.checksum());
    TEST_CHECK(one.anchorX() == four.anchorX());
    const Geography other(kSeed + 1, enabled(), WorldSize{}, 4);
    TEST_CHECK(other.checksum() != one.checksum());
    const auto a = Geography::shared(kSeed, enabled(), WorldSize{});
    const auto b = Geography::shared(kSeed, enabled(), WorldSize{});
    TEST_CHECK(a.get() == b.get());
    TEST_CHECK(a->checksum() == one.checksum());
    // The Heightfields of one seed share their geography.
    const Heightfield f(kSeed, geographyParams());
    const Heightfield g(kSeed, geographyParams());
    TEST_CHECK(f.geography() != nullptr && f.geography() == g.geography());
    TEST_CHECK(f.plates() == nullptr);
    TEST_CHECK(Heightfield(kSeed).geography() == nullptr);
    TEST_CHECK(one.memoryBytes() <= 32u * 1024u * 1024u);
}

// The planetary preset only sets the old plate and macro terms, unused by the geography.
void testPlanetaryPresetIsTheGeography() {
    HeightfieldParams preset = terrain::planetaryHeightfieldParams();
    preset.geography.enabled = true;
    const Heightfield planetary(kSeed, preset);
    const Heightfield geography(kSeed, geographyParams());
    bool identical = true;
    for (const Point& p : bit_exact::randomPoints(2'000, 3e7)) {
        identical = identical && same(planetary.sampleAt(p.x, p.z), geography.sampleAt(p.x, p.z));
    }
    TEST_CHECK(identical);
}

// sampleAt at (x, z) and at (x + k world.x, z + l world.z): the same bits, near the edges and
// the corner as anywhere; the batch functions too.
void testPeriodicAndBatch(const WorldSize& world) {
    const Heightfield field(kSeed, geographyParams(), world);
    // On the grid of 1/64 m: their images are exactly representable.
    std::vector<Point> points = bit_exact::randomPoints(3'000, 0.5 * world.x);
    for (Point& p : points) {
        p.x = std::floor(p.x * 64.0) / 64.0;
        p.z = std::floor(p.z * 64.0) / 64.0;
    }
    // The edge points whose images are exactly representable (not -0.3 world.x, say).
    for (const Point& p : bit_exact::edgePoints(world)) {
        bool exact = true;
        for (const double k : {1.0, -1.0, -2.0, 3.0}) {
            exact = exact && (p.x + k * world.x) - k * world.x == p.x &&
                    (p.z + k * world.z) - k * world.z == p.z;
        }
        if (exact) {
            points.push_back(p);
        }
    }
    bit_exact::Tally tally("geography images");
    std::vector<double> xs;
    std::vector<double> zs;
    for (const Point& p : points) {
        const terrain::HeightSample s = field.sampleAt(p.x, p.z);
        for (const auto& [kx, kz] : {std::pair{1.0, 0.0}, {0.0, -1.0}, {-2.0, 3.0}}) {
            tally.check(same(s, field.sampleAt(p.x + kx * world.x, p.z + kz * world.z)), p,
                        "image");
        }
        xs.push_back(p.x);
        zs.push_back(p.z);
    }
    std::vector<float> heights(xs.size());
    field.heightsAt(xs, zs, heights);
    for (std::size_t k = 0; k < xs.size(); ++k) {
        tally.check(same(heights[k], field.heightAt(xs[k], zs[k])), points[k], "heightsAt");
    }
}

// Below the sea exactly where the coast signal (and the continentalness) is negative. The ridged
// mountains only within their mask, which is 0 at sea and outside the belts of the plates.
void testSeaWhereTheCoastIsNegative() {
    const Heightfield field(kSeed, geographyParams());
    const float mountainHeight = field.params().geography.mountainHeight;
    int sea = 0;
    int land = 0;
    int mountains = 0;
    bool consistent = true;
    bool masked = true;
    for (const Point& p : bit_exact::randomPoints(20'000, 3e7)) {
        const terrain::HeightSample s = field.sampleAt(p.x, p.z);
        const terrain::GeographySample g =
            field.geography()->sampleAt(terrain::wrapCoordinate(p.x, WorldSize{}.x),
                                        terrain::wrapCoordinate(p.z, WorldSize{}.z));
        consistent = consistent && ((s.height < 0.0f) == (g.coast < 0.0)) &&
                     ((s.continentalness < 0.0f) == (g.coast < 0.0));
        masked = masked && s.mountain >= 0.0f && s.mountain <= s.mountainMask * mountainHeight &&
                 s.mountainMask <= 1.0f && (g.belt > 0.0f || s.mountainMask == 0.0f) &&
                 s.plateCollision == g.belt &&
                 (g.coast > 0.0 || s.mountainMask == 0.0f);
        ++(s.height < 0.0f ? sea : land);
        mountains += s.mountain > 0.0f ? 1 : 0;
    }
    TEST_CHECK(consistent);
    TEST_CHECK(masked);
    TEST_CHECK(land > 20'000 * 30 / 100 && land < 20'000 * 45 / 100);
    TEST_CHECK(mountains > 0 && mountains < land / 4);
}

// The plates leave their marks on the map: mountain belts and high plateaus on land, island arcs
// far out at sea along the belts, trenches below the abyssal plain and mid-ocean ridges above
// it.
void testTectonics() {
    const Geography& g = *Heightfield(kSeed, geographyParams()).geography();
    const GeographyParams& p = g.params();
    int land = 0;
    int belts = 0;
    int plateaus = 0;
    int arcs = 0;
    int islands = 0;
    float deepest = 0.0f;
    float highestOpenSea = p.abyssDepth;
    for (int j = 0; j < g.rows(); ++j) {
        for (int i = 0; i < g.columns(); ++i) {
            const float coast = g.node(i, j, Geography::kCoast);
            const float belt = g.node(i, j, Geography::kBelt);
            if (coast > 0.0f) {
                ++land;
                belts += belt > 0.5f ? 1 : 0;
                plateaus += g.node(i, j, Geography::kLand) > 3500.0f ? 1 : 0;
                continue;
            }
            deepest = std::min(deepest, g.node(i, j, Geography::kSea));
            if (coast < -1.0e6f) {
                highestOpenSea = std::max(highestOpenSea, g.node(i, j, Geography::kSea));
            }
            if (coast < -300'000.0f && belt > 0.5f) {
                ++arcs; // islands near the node, sampled across it
                const double x = -0.5 * g.world().x + i * g.world().x / g.columns() - g.anchorX();
                const double z = -0.5 * g.world().z + j * g.world().z / g.rows();
                bool island = false;
                for (int k = 0; k < 64 && !island; ++k) {
                    island = g.sampleAt(terrain::wrapCoordinate(x + (k % 8 - 4) * 8000.0,
                                                                g.world().x),
                                        terrain::wrapCoordinate(z + (k / 8 - 4) * 8000.0,
                                                                g.world().z))
                                 .coast > 0.0;
                }
                islands += island ? 1 : 0;
            }
        }
    }
    TEST_CHECK(belts > land / 100 && belts < land / 5);
    TEST_CHECK(plateaus > land / 500 && plateaus < land / 20);
    TEST_CHECK(arcs > 0 && islands > arcs / 10 && islands < arcs);
    TEST_CHECK(deepest < p.abyssDepth - 0.5f * p.trenchDepth);
    TEST_CHECK(highestOpenSea > p.abyssDepth + 0.5f * p.ridgeLift);
}

// Every land node of the map has a lower neighbor, or the sea next to it: water leaves every
// node of the map for the sea.
void testMapDrains() {
    const Geography& g = *Heightfield(kSeed, geographyParams()).geography();
    int land = 0;
    int pits = 0;
    for (int j = 0; j < g.rows(); ++j) {
        for (int i = 0; i < g.columns(); ++i) {
            if (g.node(i, j, Geography::kCoast) <= 0.0f) {
                continue;
            }
            ++land;
            const float here = g.node(i, j, Geography::kLand);
            bool out = false;
            for (int dj = -1; dj <= 1; ++dj) {
                for (int di = -1; di <= 1; ++di) {
                    out = out || g.node(i + di, j + dj, Geography::kCoast) <= 0.0f ||
                          g.node(i + di, j + dj, Geography::kLand) < here;
                }
            }
            pits += out ? 0 : 1;
        }
    }
    const double share = static_cast<double>(land) / (g.columns() * g.rows());
    TEST_CHECK(pits == 0);
    TEST_CHECK(std::abs(share - 0.37) < 0.02);
    TEST_CHECK(g.continents() >= 5 && g.continents() <= 10);
}

// The origin lies on a coast of a continent: land 300 km away along the equator, on one side,
// over 100 km from the coast of the map.
void testOriginOnACoast() {
    const Geography& g = *Heightfield(kSeed, geographyParams()).geography();
    TEST_CHECK(std::abs(g.sampleAt(0.0, 0.0).coast) < 10'000.0);
    const double west = g.sampleAt(terrain::wrapCoordinate(-300'000.0, g.world().x), 0.0).mapCoast;
    const double east = g.sampleAt(300'000.0, 0.0).mapCoast;
    TEST_CHECK(std::max(west, east) > 100'000.0);
}

void testSmallWorlds() {
    for (const WorldSize& world : {WorldSize{65536.0, 3.0 * 65536.0},
                                   WorldSize{32.0 * 65536.0, 16.0 * 65536.0}}) {
        const Heightfield field(kSeed, geographyParams(), world);
        TEST_CHECK(field.geography() != nullptr);
        TEST_CHECK(std::isfinite(field.heightAt(1234.5, -678.25)));
        testPeriodicAndBatch(world);
    }
}

void testInvalidParameters() {
    const auto rejects = [](auto change) {
        GeographyParams params = enabled();
        change(params);
        try {
            const Geography g(kSeed, params, WorldSize{});
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    TEST_CHECK(rejects([](GeographyParams& p) { p.cellSize = 0.0; }));
    TEST_CHECK(rejects([](GeographyParams& p) { p.landShare = 1.0f; }));
    TEST_CHECK(rejects([](GeographyParams& p) { p.crustCandidates = 0; }));
    TEST_CHECK(rejects([](GeographyParams& p) { p.coastBand = p.coastAmplitude; }));
    TEST_CHECK(rejects([](GeographyParams& p) { p.drainSlope = -1.0; }));
    TEST_CHECK(rejects([](GeographyParams& p) { p.beltWidth = 0.0; }));
    TEST_CHECK(rejects([](GeographyParams& p) { p.arcLift = -1.0; }));
    TEST_CHECK(rejects([](GeographyParams& p) { p.plates.minPlates = 2; }));
}

} // namespace

int main() {
    testDeterminismAndSharing();
    testPlanetaryPresetIsTheGeography();
    testPeriodicAndBatch(WorldSize{});
    testSeaWhereTheCoastIsNegative();
    testMapDrains();
    testTectonics();
    testOriginOnACoast();
    testSmallWorlds();
    testInvalidParameters();
    return test::result();
}
