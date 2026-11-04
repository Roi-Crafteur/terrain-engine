// The runoff and its infiltration (runoff_partition.hpp, task 11C step 1): the share that runs off
// follows the rock, the slope, the plants, the frost and the storms; the channel heads, dense on
// the wet impermeable slopes, rare on the dry permeable plains; the permeability of the geology.
#include "terrain/generation/geography.hpp"
#include "terrain/generation/runoff_partition.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using terrain::partitionRunoff;
using terrain::RunoffParams;
using terrain::RunoffSite;

RunoffSite temperate() {
    RunoffSite site;
    site.precipitation = 900.0f;
    site.pet = 700.0f;
    site.temperature = 10.0f;
    site.latitude = 45.0f;
    site.permeability = 0.4f;
    site.slope = 0.1f;
    return site;
}

void testShares() {
    const RunoffParams params;
    const RunoffSite base = temperate();
    const float s = partitionRunoff(base, params).surface;
    // An impermeable ground lets more run off, a permeable one less.
    RunoffSite clay = base;
    clay.permeability = 0.05f;
    RunoffSite chalk = base;
    chalk.permeability = 0.85f;
    TEST_CHECK(partitionRunoff(clay, params).surface > s);
    TEST_CHECK(partitionRunoff(chalk, params).surface < s);
    // Steeper, more.
    RunoffSite steep = base;
    steep.slope = 0.8f;
    TEST_CHECK(partitionRunoff(steep, params).surface > s);
    // A wetter climate grows a forest that takes the water in.
    RunoffSite forest = base;
    forest.precipitation = 1600.0f;
    TEST_CHECK(partitionRunoff(forest, params).vegetation >
               partitionRunoff(base, params).vegetation);
    TEST_CHECK(partitionRunoff(forest, params).surface < s);
    // Frozen ground takes nothing in.
    RunoffSite frozen = base;
    frozen.temperature = -15.0f;
    TEST_CHECK(partitionRunoff(frozen, params).frost == 1.0f);
    TEST_CHECK(partitionRunoff(frozen, params).surface > s);
    // The storms of the tropics.
    RunoffSite tropics = base;
    tropics.latitude = 5.0f;
    TEST_CHECK(partitionRunoff(tropics, params).storm > partitionRunoff(base, params).storm);
    TEST_CHECK(partitionRunoff(tropics, params).surface > s);
    // Always within [leastSurface, 1].
    for (const float k : {0.0f, 0.5f, 1.0f}) {
        for (const float slope : {0.0f, 0.3f, 3.0f}) {
            RunoffSite any = base;
            any.permeability = k;
            any.slope = slope;
            const float share = partitionRunoff(any, params).surface;
            TEST_CHECK(share >= params.leastSurface && share <= 1.0f);
        }
    }
}

void testHeads() {
    const RunoffParams params;
    // A wet mountain of crystalline rock: channels a few hundredths of a square km apart.
    RunoffSite mountain = temperate();
    mountain.precipitation = 1800.0f;
    mountain.permeability = 0.12f;
    mountain.slope = 0.6f;
    // A dry plain of limestone: few channels.
    RunoffSite plain = temperate();
    plain.precipitation = 250.0f;
    plain.pet = 1100.0f;
    plain.temperature = 18.0f;
    plain.permeability = 0.8f;
    plain.slope = 0.005f;
    const double wet = partitionRunoff(mountain, params).headArea;
    const double dry = partitionRunoff(plain, params).headArea;
    std::printf("channel heads: %.3f km^2 on a wet mountain, %.2f km^2 on a dry limestone plain\n",
                wet * 1e-6, dry * 1e-6);
    TEST_CHECK(wet < 0.1e6);
    TEST_CHECK(dry > 2.0e6);
    TEST_CHECK(wet >= params.leastHead);
    TEST_CHECK(dry <= params.mostHead + 0.8 * params.permeableHead + params.aridHead + 1.0);
    // The base flow keeps a brook wet; a desert channel is dry.
    TEST_CHECK(terrain::channelWet(0.01, 0.3f, params));
    TEST_CHECK(!terrain::channelWet(0.0005, 0.3f, params));
    TEST_CHECK(!terrain::channelWet(0.0, 0.9f, params));
}

// The permeability of the default world: hard under the mountains, from clays to limestones on
// the plains, the sand of the ergs open; the same bits for the same seed, others for another.
void testPermeability() {
    const terrain::Heightfield relief(terrain::kDefaultWorldSeed);
    const terrain::Permeability ground(terrain::kDefaultWorldSeed, relief.world());
    const terrain::Permeability other(terrain::kDefaultWorldSeed + 1, relief.world());
    std::vector<float> mountains;
    std::vector<float> plains;
    int differs = 0;
    for (int n = 0; n < 4000; ++n) {
        const double x = -3.0e7 + 15'003.7 * n;
        const double z = -1.2e7 + 6'000.3 * (n % 4000);
        const terrain::HeightSample s = relief.sampleAt(x, z);
        if (s.water != terrain::WaterKind::None) {
            continue;
        }
        const float character = relief.geography()->sampleAt(x, z).character;
        const float k = ground.at(x, z, s, character);
        TEST_CHECK(k >= 0.0f && k <= 1.0f);
        TEST_CHECK(k == ground.at(x, z, s, character));
        differs += other.at(x, z, s, character) != k ? 1 : 0;
        if (s.mountainMask > 0.5f) {
            mountains.push_back(k);
        } else if (s.mountainMask == 0.0f && s.relief < 0.3f && s.rift == 0.0f) {
            plains.push_back(k);
        }
    }
    std::sort(mountains.begin(), mountains.end());
    std::sort(plains.begin(), plains.end());
    std::printf("permeability: %zu mountain points, median %.2f; %zu plain points, from %.2f to "
                "%.2f (median %.2f)\n",
                mountains.size(), mountains[mountains.size() / 2], plains.size(), plains.front(),
                plains.back(), plains[plains.size() / 2]);
    TEST_CHECK(!mountains.empty() && mountains[mountains.size() / 2] < 0.25f);
    TEST_CHECK(plains.size() > 100 && plains[plains.size() / 20] < 0.2f);
    TEST_CHECK(plains[plains.size() * 19 / 20] > 0.6f);
    TEST_CHECK(differs > 100);
}

} // namespace

int main() {
    testShares();
    testHeads();
    testPermeability();
    return test::result();
}
