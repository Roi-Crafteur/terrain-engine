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
#include <utility>
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
// the sea; its floor never a plane: the relief of the plains, a little of it, with salt pans and
// buttes.
void testSites() {
    const std::vector<Depression> sites = field().depressions();
    std::printf("%zu depressions\n", sites.size());
    TEST_CHECK(sites.size() >= 20 && sites.size() <= 100);
    bool dry = true;
    bool below = true;
    bool inland = true;
    bool relief = true;
    for (const Depression& d : sites) {
        const HeightSample s = field().sampleAt(d.x, d.z);
        dry = dry && s.water == WaterKind::None && s.waterLevel == s.height;
        below = below && s.height < 0.0f && d.floor < 0.0f && s.height > d.floor - 40.0f &&
                s.height < d.floor + 130.0f;
        inland = inland && field().geography()->sampleAt(d.x, d.z).coast > 300'000.0;
        // Within 2 km of the center the floor rises and falls by meters, by 200 m at most (its
        // buttes and vallons).
        double sum = 0.0;
        double squares = 0.0;
        float low = 1e9f;
        float high = -1e9f;
        for (int j = -20; j <= 20; ++j) {
            for (int i = -20; i <= 20; ++i) {
                const float h = field().heightAt(d.x + 100.0 * i, d.z + 100.0 * j);
                sum += h;
                squares += static_cast<double>(h) * h;
                low = std::min(low, h);
                high = std::max(high, h);
            }
        }
        const double n = 41.0 * 41.0;
        const double deviation = std::sqrt(std::max(0.0, squares / n - (sum / n) * (sum / n)));
        relief = relief && deviation > 0.5 && high - low < 200.0f;
    }
    TEST_CHECK(dry);
    TEST_CHECK(below);
    TEST_CHECK(inland);
    TEST_CHECK(relief);
    TEST_CHECK(withoutDepressions().depressions().empty());
}

// The highest cliff of the deepest depression: the greatest rise outward over 400 m along rays
// around its escarpment, and the steepest slope over 25 m there.
void testCliffAndPans() {
    const std::vector<Depression> sites = field().depressions();
    const Depression d = *std::min_element(
        sites.begin(), sites.end(), [](const auto& a, const auto& b) { return a.floor < b.floor; });
    double rise = 0.0;
    double steepest = 0.0;
    for (int k = 0; k < 32; ++k) {
        const double t = 6.283185307179586 * (d.escarpment + 0.5 * (k / 32.0 - 0.5));
        std::vector<float> h;
        for (double r = 0.3 * d.radius; r < 2.2 * d.radius; r += 25.0) {
            h.push_back(field().heightAt(d.x + r * std::cos(t), d.z + r * std::sin(t)));
        }
        for (std::size_t i = 0; i + 16 < h.size(); ++i) {
            rise = std::max(rise, static_cast<double>(h[i + 16] - h[i]));
            steepest = std::max(steepest, static_cast<double>(h[i + 1] - h[i]) / 25.0);
        }
    }
    // Flat salt pans on its floor: points whose neighbors 25 m away are within 5 cm.
    int flat = 0;
    for (int j = -60; j <= 60 && flat == 0; ++j) {
        for (int i = -60; i <= 60; ++i) {
            const double x = d.x + 100.0 * i;
            const double z = d.z + 100.0 * j;
            const float h = field().heightAt(x, z);
            bool same = h < d.floor;
            for (const auto& [a, b] :
                 {std::pair{25.0, 0.0}, {-25.0, 0.0}, {0.0, 25.0}, {0.0, -25.0}}) {
                same = same && std::abs(field().heightAt(x + a, z + b) - h) < 0.05f;
            }
            flat += same ? 1 : 0;
        }
    }
    std::printf("deepest depression: cliff of %.0f m over 400 m, steepest %.2f over 25 m, "
                "pans found: %d\n",
                rise, steepest, flat);
    TEST_CHECK(rise >= 80.0);
    TEST_CHECK(steepest >= 0.58); // 30 degrees
    TEST_CHECK(flat > 0);
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

// Continuous from the center of every depression out across its escarpment: the largest rise
// between samples 2 m apart is a steep face, not a step (sampled every 10 cm, it rises by less
// than a fifth of it at a time); the same bits in batches and alone.
void testContinuousAndBatched() {
    bool same = true;
    float largest = 0.0f;
    double atX = 0.0;
    double atZ = 0.0;
    double dirX = 0.0;
    double dirZ = 0.0;
    for (const Depression& d : field().depressions()) {
        std::vector<double> xs;
        std::vector<double> zs;
        const double c = std::cos(6.283185307179586 * d.escarpment);
        const double n = std::sin(6.283185307179586 * d.escarpment);
        for (double r = 0.0; r < 1.8 * d.radius; r += 2.0) {
            xs.push_back(d.x + r * c);
            zs.push_back(d.z + r * n);
        }
        std::vector<HeightSample> batch(xs.size());
        field().samplesAt(xs, zs, batch);
        for (std::size_t i = 0; i < xs.size(); ++i) {
            if (i % 97 == 0) {
                same = same && sameSample(batch[i], field().sampleAt(xs[i], zs[i]));
            }
            const float step = i > 0 ? std::abs(batch[i].height - batch[i - 1].height) : 0.0f;
            if (step > largest) {
                largest = step;
                atX = xs[i - 1];
                atZ = zs[i - 1];
                dirX = c;
                dirZ = n;
            }
        }
    }
    float finest = 0.0f;
    for (int k = 0; k < 20; ++k) {
        const float a = field().heightAt(atX + 0.1 * k * dirX, atZ + 0.1 * k * dirZ);
        const float b = field().heightAt(atX + 0.1 * (k + 1) * dirX, atZ + 0.1 * (k + 1) * dirZ);
        finest = std::max(finest, std::abs(b - a));
    }
    std::printf("largest rise over 2 m across the escarpments: %.2f m at (%.1f, %.1f), over 10 "
                "cm there: %.2f m\n",
                static_cast<double>(largest), atX, atZ, static_cast<double>(finest));
    TEST_CHECK(same);
    TEST_CHECK(finest < 0.2f * largest);
    TEST_CHECK(largest < 10.0f);
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
    testCliffAndPans();
    testMountainsKeepTheirBits();
    testContinuousAndBatched();
    testSurface();
    testSeaOfTheGeography();
    testSeaConnectedToTheOcean();
    return test::result();
}
