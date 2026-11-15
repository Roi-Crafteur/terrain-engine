// The lows of agent 52 all on (the flag "lows": the families of LowsParams, the sunken basins of
// the geography), against the default world:
// - the sea never moves: at random points of the world, a sample is the ocean with the lows on
//   exactly where it is without them (the coast of the geography decides, never the height);
//   some ground with the lows on lies below the sea level and is not the sea;
// - the cost: rows of samples on the plains far from the sea, where the lows are looked for at
//   every sample (the hollows are implicit), cost at most 1.3 times as much with them; in the
//   mountains, where none may lie, as much.
#include "terrain/generation/experimental_flags.hpp"
#include "terrain/generation/heightfield.hpp"

#include "cost_ratio_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::HeightSample;
using terrain::WaterKind;

const Heightfield& field(bool lows) {
    static const auto make = [](bool on) {
        terrain::WorldParams p;
        if (on) {
            static_cast<void>(terrain::applyExperimentalFlags("lows", p));
        }
        return Heightfield(terrain::kDefaultWorldSeed, p.relief);
    };
    static const Heightfield with = make(true);
    static const Heightfield without = make(false);
    return lows ? with : without;
}

void testSea() {
    const terrain::WorldSize& w = field(false).world();
    std::mt19937_64 rng(52);
    std::uniform_real_distribution<double> ux(-0.5 * w.x, 0.5 * w.x);
    std::uniform_real_distribution<double> uz(-0.5 * w.z, 0.5 * w.z);
    std::size_t sea = 0;
    std::size_t moved = 0;
    std::size_t dryBelow = 0;
    for (int n = 0; n < 40'000; ++n) {
        const double x = ux(rng);
        const double z = uz(rng);
        const HeightSample a = field(true).sampleAt(x, z);
        const HeightSample b = field(false).sampleAt(x, z);
        sea += b.water == WaterKind::Ocean ? 1 : 0;
        moved += (a.water == WaterKind::Ocean) != (b.water == WaterKind::Ocean) ? 1 : 0;
        dryBelow += a.water != WaterKind::Ocean && a.height < terrain::kSeaLevel ? 1 : 0;
    }
    std::printf("of 40000 points: %zu sea, %zu moved; %zu below the sea level on land, lows on\n",
                sea, moved, dryBelow);
    TEST_CHECK(sea > 10'000 && moved == 0 && dryBelow > 0);
}

// Rows of kRow samples a meter apart at the centers given, on and off.
cost_test::Ratio rowsCost(const std::vector<std::array<double, 2>>& centers, double most) {
    constexpr std::size_t kRow = 2048;
    const cost_test::TimedRun run = [&](cost_test::Side side, std::size_t item) {
        const Heightfield& f = field(side == cost_test::Side::With);
        std::vector<double> xs(kRow);
        std::vector<double> zs(kRow, centers[item][1]);
        for (std::size_t k = 0; k < kRow; ++k) {
            xs[k] = centers[item][0] + static_cast<double>(k);
        }
        std::vector<HeightSample> out(kRow);
        return cost_test::secondsOf([&] { f.samplesAt(xs, zs, out); });
    };
    return cost_test::ratioAtMost(centers.size(), run, most);
}

void testCost() {
    // Plains far from the sea and mountains of the default world, by their samples.
    std::vector<std::array<double, 2>> plains;
    std::vector<std::array<double, 2>> mountains;
    const terrain::WorldSize& w = field(false).world();
    std::mt19937_64 rng(5);
    std::uniform_real_distribution<double> ux(-0.5 * w.x, 0.5 * w.x);
    std::uniform_real_distribution<double> uz(-0.4 * w.z, 0.4 * w.z);
    while (plains.size() < 8 || mountains.size() < 4) {
        const double x = ux(rng);
        const double z = uz(rng);
        const HeightSample s = field(false).sampleAt(x, z);
        const double coast = static_cast<double>(s.continentalness) * 1.5e6;
        if (s.relief < 0.1f && coast > 100'000.0 && plains.size() < 8) {
            plains.push_back({x, z});
        } else if (s.mountainMask > 0.5f && mountains.size() < 4) {
            mountains.push_back({x, z});
        }
    }
    const cost_test::Ratio onPlains = rowsCost(plains, 1.3);
    const cost_test::Ratio inMountains = rowsCost(mountains, 1.1);
    std::printf("cost on the plains %s\ncost in the mountains %s\n",
                cost_test::ratioText(onPlains).c_str(), cost_test::ratioText(inMountains).c_str());
    TEST_CHECK(onPlains.passed() && inMountains.passed());
}

} // namespace

int main() {
    testSea();
    testCost();
    return test::result();
}
