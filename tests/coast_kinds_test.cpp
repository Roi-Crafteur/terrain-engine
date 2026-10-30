// The kinds of coast (agent 33, coast.hpp) of the default world: their shares along the coasts of
// the whole planet (points of the shore drawn over the world, weighted by the length of coast they
// stand for), in the ranges of the plan (to be adjusted on the maps); weights summing to 1, smooth
// from meter to meter (no edge between two kinds), the low coast alone out of the band and without
// the coasts. The kinds read the geography of the Heightfield only (the intensity, character and
// ruggedness of its relief, its strata) and the latitude: no climate, no biome is an input of
// Heightfield::coastAt.
#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"

#include "coast_zone_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace {

using terrain::CoastKind;
using terrain::CoastSample;
using terrain::Heightfield;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr int kShorePoints = 4'000;
// The shore: land within kShore meters of coast signal of the sea.
constexpr double kShore = 3'000.0;

const Heightfield& field() {
    static const Heightfield f(kSeed);
    return f;
}

struct Range {
    CoastKind kind;
    double low; // shares of the length of the coasts, in [0, 1]
    double high;
};

constexpr std::array<Range, terrain::kCoastKindCount> kRanges{{
    {CoastKind::Low, 0.35, 0.55},
    {CoastKind::Rocky, 0.10, 0.25},
    {CoastKind::Cliff, 0.10, 0.25},
    {CoastKind::Calanque, 0.003, 0.03},
    {CoastKind::Ria, 0.01, 0.06},
    {CoastKind::Mountain, 0.05, 0.15},
    {CoastKind::Fjord, 0.01, 0.06},
}};

// The length of coast a point of the shore stands for grows with the gradient of the coast signal
// there (the band of kShore of signal is that much narrower).
double gradientAt(double x, double z) {
    constexpr double h = 8.0;
    const double gx = (coast_zone::coastSignal(field(), x + h, z) -
                       coast_zone::coastSignal(field(), x - h, z)) /
                      (2.0 * h);
    const double gz = (coast_zone::coastSignal(field(), x, z + h) -
                       coast_zone::coastSignal(field(), x, z - h)) /
                      (2.0 * h);
    return std::hypot(gx, gz);
}

void testShares() {
    coast_zone::Draws draws(0x33C0A57D1ULL);
    const terrain::WorldSize& w = field().world();
    std::array<double, terrain::kCoastKindCount> weighted{};
    std::array<double, terrain::kCoastKindCount> heaviest{};
    double total = 0.0;
    int points = 0;
    while (points < kShorePoints) {
        const double x = (draws.next() - 0.5) * w.x;
        const double z = (draws.next() - 0.5) * w.z;
        const CoastSample c = field().coastAt(x, z);
        if (!(c.base > 0.0 && c.base < kShore)) {
            continue;
        }
        const double length = gradientAt(x, z);
        for (std::size_t k = 0; k < weighted.size(); ++k) {
            weighted[k] += length * static_cast<double>(c.weights[k]);
        }
        heaviest[static_cast<std::size_t>(c.kind)] += length;
        total += length;
        ++points;
    }
    bool within = true;
    for (const Range& r : kRanges) {
        const auto k = static_cast<std::size_t>(r.kind);
        const double share = weighted[k] / total;
        std::printf("%-9s %5.1f %% of the coasts (%5.1f %% heaviest), wanted %.1f to %.1f %%\n",
                    terrain::coastKindName(r.kind), 100.0 * share, 100.0 * heaviest[k] / total,
                    100.0 * r.low, 100.0 * r.high);
        within = within && share >= r.low && share <= r.high;
    }
    TEST_CHECK(within);
}

// Weights summing to 1; the low coast alone out of the band and without the coasts; the signal
// of the coasts that of the geography (no inlet yet).
void testWeights() {
    coast_zone::Draws draws(0x33C0A57D2ULL);
    const terrain::WorldSize& w = field().world();
    const Heightfield without = field().withoutCoasts();
    TEST_CHECK(field().hasCoasts() && !without.hasCoasts());
    TEST_CHECK(!without.params().geography.coasts);
    int inBand = 0;
    bool sums = true;
    bool alone = true;
    for (int i = 0; i < 20'000; ++i) {
        const double x = (draws.next() - 0.5) * w.x;
        const double z = (draws.next() - 0.5) * w.z;
        const CoastSample c = field().coastAt(x, z);
        float sum = 0.0f;
        for (const float v : c.weights) {
            sum += v;
            sums = sums && v >= 0.0f && v <= 1.0f;
        }
        sums = sums && std::abs(sum - 1.0f) < 1e-5f;
        TEST_CHECK(c.inBand == coast_zone::inZone(c.base));
        const CoastSample bare = without.coastAt(x, z);
        alone = alone && bare.weight(CoastKind::Low) == 1.0f && bare.base == c.base &&
                bare.signal == c.base && (c.inBand || c.weight(CoastKind::Low) == 1.0f);
        inBand += c.inBand ? 1 : 0;
    }
    std::printf("%d of 20000 points in the band of the coasts\n", inBand);
    TEST_CHECK(sums);
    TEST_CHECK(alone);
    TEST_CHECK(inBand > 100);
}

// No edge between two kinds: along lines across the band, the weights change by less than
// kMostPerMeter per meter.
void testSmooth() {
    constexpr double kMostPerMeter = 2e-3;
    coast_zone::Draws draws(0x33C0A57D3ULL);
    const terrain::WorldSize& w = field().world();
    double most = 0.0;
    int lines = 0;
    while (lines < 40) {
        const double x = (draws.next() - 0.5) * w.x;
        const double z = (draws.next() - 0.5) * w.z;
        if (!field().coastAt(x, z).inBand) {
            continue;
        }
        CoastSample previous = field().coastAt(x, z);
        for (int i = 1; i < 2'000; ++i) {
            const CoastSample c = field().coastAt(x + i, z);
            if (c.inBand && previous.inBand) {
                for (std::size_t k = 0; k < c.weights.size(); ++k) {
                    most = std::max(most, static_cast<double>(std::abs(c.weights[k] -
                                                                       previous.weights[k])));
                }
            }
            previous = c;
        }
        ++lines;
    }
    std::printf("weights of the kinds: at most %.5f of change per meter\n", most);
    TEST_CHECK(most < kMostPerMeter);
}

} // namespace

int main() {
    testWeights();
    testSmooth();
    testShares();
    return test::result();
}
