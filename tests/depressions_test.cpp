// The depressions below the sea level of phase 11B and the sea decided by the geography: dry land
// below the sea level deep in the plains, never under the mountains (they keep every bit) nor
// near the sea; the sea where the coast signal of the geography is negative, connected to the
// ocean on the planet level of the small world.
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include "bit_exact_support.hpp"
#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::Depression;
using terrain::Heightfield;
using terrain::HeightSample;
using terrain::WaterKind;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
// The continentalness of the Heightfield is the coast distance over this scale.
constexpr double kContinentalScale = 1'500'000.0;

const Heightfield& field() {
    static const Heightfield f(kSeed);
    return f;
}

const Heightfield& withoutDepressions() {
    static const Heightfield f = [] {
        terrain::HeightfieldParams params;
        params.geography.depressions = false;
        return Heightfield(kSeed, params);
    }();
    return f;
}

bool sameSample(const HeightSample& a, const HeightSample& b) {
    return bit_exact::same(a.base, b.base) && bit_exact::same(a.mountain, b.mountain) &&
           bit_exact::same(a.detail, b.detail) && bit_exact::same(a.height, b.height) &&
           bit_exact::same(a.mountainMask, b.mountainMask);
}

// Tens of depressions over the world, each one dry land below the sea at its center, far from
// the sea, its floor flat.
void testSites() {
    const std::vector<Depression> sites = field().depressions();
    std::printf("%zu depressions\n", sites.size());
    TEST_CHECK(sites.size() >= 20 && sites.size() <= 100);
    bool dry = true;
    bool below = true;
    bool inland = true;
    bool flat = true;
    for (const Depression& d : sites) {
        const HeightSample s = field().sampleAt(d.x, d.z);
        dry = dry && s.water == WaterKind::None && s.waterLevel == s.height;
        below = below && s.height < 0.0f && d.floor < 0.0f && std::abs(s.height - d.floor) < 10.0f;
        inland = inland && field().geography()->sampleAt(d.x, d.z).coast > 300'000.0;
        // The floor varies by the detail only within a kilometer of the center.
        for (int k = 0; k < 8; ++k) {
            const double a = 0.125 * k;
            const float h = field().heightAt(d.x + 1000.0 * std::cos(6.283185307 * a),
                                             d.z + 1000.0 * std::sin(6.283185307 * a));
            flat = flat && std::abs(h - s.height) < 15.0f;
        }
    }
    TEST_CHECK(dry);
    TEST_CHECK(below);
    TEST_CHECK(inland);
    TEST_CHECK(flat);
    TEST_CHECK(withoutDepressions().depressions().empty());
}

// Wherever the depressions change a sample, the mountain mask is 0 and the sea is far: the
// mountains keep their bits. Over the world, and around every depression.
void testMountainsKeepTheirBits() {
    int changed = 0;
    int mountains = 0;
    bool untouched = true;
    const auto compare = [&](double x, double z) {
        const HeightSample a = field().sampleAt(x, z);
        const HeightSample b = withoutDepressions().sampleAt(x, z);
        mountains += b.mountainMask > 0.0f ? 1 : 0;
        if (sameSample(a, b)) {
            return;
        }
        ++changed;
        untouched = untouched && a.mountainMask == 0.0f && b.mountainMask == 0.0f &&
                    a.relief < 0.12f &&
                    static_cast<double>(a.continentalness) * kContinentalScale > 149'000.0 &&
                    a.water == WaterKind::None;
    };
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 3e7)) {
        compare(p.x, p.z);
    }
    for (const Depression& d : field().depressions()) {
        for (int j = -12; j <= 12; ++j) {
            for (int i = -12; i <= 12; ++i) {
                compare(d.x + i * d.radius / 4.0, d.z + j * d.radius / 4.0);
            }
        }
    }
    std::printf("%d samples changed, %d under the mountains\n", changed, mountains);
    TEST_CHECK(untouched);
    TEST_CHECK(changed > 1'000);
}

// Continuous: no step between samples 4 m apart across a depression (it makes the ground at most
// a little steeper than without it: gentle sides, an escarpment of a few degrees on one of them),
// the same bits in batches and alone.
void testContinuousAndBatched() {
    const Depression d = field().depressions().front();
    std::vector<double> xs;
    std::vector<double> zs;
    for (int i = -2000; i <= 2000; ++i) {
        xs.push_back(d.x + 4.0 * d.radius * i / 2000.0);
        zs.push_back(d.z + 0.3 * d.radius * i / 2000.0);
    }
    std::vector<HeightSample> batch(xs.size());
    std::vector<HeightSample> before(xs.size());
    field().samplesAt(xs, zs, batch);
    withoutDepressions().samplesAt(xs, zs, before);
    bool same = true;
    double added = 0.0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        same = same && sameSample(batch[i], field().sampleAt(xs[i], zs[i]));
        if (i > 0) {
            const double run = std::hypot(xs[i] - xs[i - 1], zs[i] - zs[i - 1]);
            const double slope =
                std::abs(static_cast<double>(batch[i].height - batch[i - 1].height)) / run;
            const double was =
                std::abs(static_cast<double>(before[i].height - before[i - 1].height)) / run;
            added = std::max(added, slope - was);
        }
    }
    std::printf("steepest slope added across a depression: %.3f\n", added);
    TEST_CHECK(same);
    TEST_CHECK(added < 0.3);
}

// The surface of a depression: dry land below the sea, never the sea nor a beach.
void testSurface() {
    const Depression d = field().depressions().front();
    const terrain::HeightmapRegion region{d.x, d.z, 500.0, 32};
    const terrain::TerrainSurface surface = terrain::surfaceFromHeightfield(field(), region);
    const terrain::Ecology ecology(kSeed);
    const terrain::EcologyMap map = ecology.map(surface);
    bool dry = true;
    bool noBeach = true;
    for (std::size_t i = 0; i < surface.sites.size(); ++i) {
        const terrain::SurfaceSite& site = surface.sites[i];
        dry = dry && site.ground < 0.0f && site.dry();
        noBeach = noBeach && map.samples[i].biome != terrain::Biome::Beach &&
                  map.samples[i].biome != terrain::Biome::Ocean;
    }
    TEST_CHECK(dry);
    TEST_CHECK(noBeach);
}

// The sea of the samples is that of the geography: below the sea level, its coast signal
// negative.
void testSeaOfTheGeography() {
    int sea = 0;
    bool consistent = true;
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 3e7)) {
        const HeightSample s = field().sampleAt(p.x, p.z);
        const terrain::GeographySample g = field().geography()->sampleAt(
            terrain::wrapCoordinate(p.x, terrain::WorldSize{}.x),
            terrain::wrapCoordinate(p.z, terrain::WorldSize{}.z));
        const bool atSea = s.water == WaterKind::Ocean;
        consistent = consistent && atSea == (g.coast < 0.0) &&
                     (!atSea || (s.height < 0.0f && s.waterLevel == terrain::kSeaLevel)) &&
                     (atSea || s.waterLevel == s.height);
        sea += atSea ? 1 : 0;
    }
    TEST_CHECK(consistent);
    TEST_CHECK(sea > 20'000 * 55 / 100 && sea < 20'000 * 70 / 100);
}

// On the planet level of the small world, the sea cells are the ocean, connected to it, but for
// a few small pockets the geography encloses (lakes for the planet level).
void testSeaConnectedToTheOcean() {
    const terrain::Heightfield small(kSeed, terrain::HeightfieldParams{}, golden::kSmallPlanet);
    const terrain::PlanetGrid grid = terrain::makePlanetGrid(small.world(), 8192.0);
    const terrain::planet::CellRelief cells = terrain::planet::sampleCells(small, grid, 1, 2);
    const std::vector<std::uint8_t> ocean =
        terrain::planet::markOcean(grid, cells.height, cells.sea, 1.0e10);
    std::size_t sea = 0;
    std::size_t connected = 0;
    bool onlySea = true;
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        sea += cells.sea[c];
        connected += ocean[c] == terrain::planet::kOcean ? 1 : 0;
        onlySea = onlySea && (ocean[c] != terrain::planet::kOcean || cells.sea[c] != 0);
    }
    std::printf("small world: %zu sea cells, %zu of the ocean\n", sea, connected);
    TEST_CHECK(onlySea);
    TEST_CHECK(connected * 100 >= sea * 99);
    const terrain::planet::CellRelief three = terrain::planet::sampleCells(small, grid, 1, 3);
    TEST_CHECK(three.sea == cells.sea);
    TEST_CHECK(std::equal(three.height.begin(), three.height.end(), cells.height.begin(),
                          [](float a, float b) { return bit_exact::same(a, b); }));
}

} // namespace

int main() {
    testSites();
    testMountainsKeepTheirBits();
    testContinuousAndBatched();
    testSurface();
    testSeaOfTheGeography();
    testSeaConnectedToTheOcean();
    return test::result();
}
