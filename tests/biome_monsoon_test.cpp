// The zones of the monsoon (agent 49, monsoon.hpp): a marking in [0, 1], never a biome.
// - none where it is too cold or too dry, wherever the point;
// - full in the heart of the band of the tropics for a hot and wet climate, none near the poles;
// - the edge of the band is no parallel: the latitude where it crosses half its share moves by
//   degrees from one meridian to another (the slow warp);
// - the same share for the same seed, at every image of a point; invalid parameters refused.
#include "terrain/generation/monsoon.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace {

constexpr std::uint64_t kSeed = 20261019;

terrain::ClimateSample climateOf(float baseTemperature, float humidity) {
    terrain::ClimateSample c;
    c.baseTemperature = baseTemperature;
    c.temperature = baseTemperature;
    c.humidity = humidity;
    return c;
}

// The z of a latitude in degrees, in the northern half (z < 0).
double zOf(const terrain::WorldSize& world, double latitude) {
    return -latitude / 180.0 * world.z;
}

void testDryAndCold(const terrain::Monsoon& m, const terrain::WorldSize& world) {
    bool none = true;
    for (int i = 0; i < 64; ++i) {
        const double x = (i / 64.0 - 0.5) * world.x;
        const double z = zOf(world, 15.0);
        none = none && m.shareAt(x, z, climateOf(12.0f, 0.9f)) == 0.0f;
        none = none && m.shareAt(x, z, climateOf(30.0f, 0.2f)) == 0.0f;
    }
    TEST_CHECK(none);
}

void testBand(const terrain::Monsoon& m, const terrain::WorldSize& world) {
    const terrain::ClimateSample hot = climateOf(30.0f, 0.9f);
    float heart = 1.0f;
    float poles = 0.0f;
    bool inRange = true;
    for (int i = 0; i < 64; ++i) {
        const double x = (i / 64.0 - 0.5) * world.x;
        heart = std::min(heart, m.shareAt(x, zOf(world, 16.5), hot));
        poles = std::max(poles, m.shareAt(x, zOf(world, 45.0), hot));
        for (int lat = 0; lat <= 90; lat += 5) {
            const float s = m.shareAt(x, zOf(world, lat), hot);
            inRange = inRange && s >= 0.0f && s <= 1.0f;
        }
    }
    std::printf("hot and wet: share at least %.2f at 16.5 degrees, at most %.2f at 45\n",
                static_cast<double>(heart), static_cast<double>(poles));
    TEST_CHECK(heart == 1.0f && poles == 0.0f && inRange);
}

// The latitude where the share crosses one half on the outer edge of the band, along a meridian.
double outerEdge(const terrain::Monsoon& m, const terrain::WorldSize& world, double x) {
    const terrain::ClimateSample hot = climateOf(30.0f, 0.9f);
    double inside = 16.5;
    double outside = 45.0;
    for (int n = 0; n < 40; ++n) {
        const double mid = 0.5 * (inside + outside);
        (m.shareAt(x, zOf(world, mid), hot) >= 0.5f ? inside : outside) = mid;
    }
    return inside;
}

void testWavyEdge(const terrain::Monsoon& m, const terrain::WorldSize& world) {
    double low = 90.0;
    double high = 0.0;
    for (int i = 0; i < 128; ++i) {
        const double edge = outerEdge(m, world, (i / 128.0 - 0.5) * world.x);
        low = std::min(low, edge);
        high = std::max(high, edge);
    }
    std::printf("the outer edge of the band lies from %.2f to %.2f degrees\n", low, high);
    TEST_CHECK(high - low > 2.0);
}

void testDeterminism(const terrain::Monsoon& m, const terrain::WorldSize& world) {
    const terrain::Monsoon again(kSeed, world);
    const terrain::ClimateSample c = climateOf(26.0f, 0.55f);
    bool same = true;
    for (int i = 0; i < 64; ++i) {
        const double x = (i / 64.0 - 0.5) * world.x;
        const double z = zOf(world, 2.0 + i * 0.5);
        // The same bits for the same point; an image of it, whose coordinates round otherwise,
        // the same share within a hair.
        same = same && m.shareAt(x, z, c) == again.shareAt(x, z, c) &&
               std::abs(m.shareAt(x, z, c) - m.shareAt(x + world.x, z + world.z, c)) < 1e-4f;
    }
    TEST_CHECK(same);
    int refused = 0;
    for (int k = 0; k < 3; ++k) {
        terrain::MonsoonParams p;
        if (k == 0) {
            p.warmHigh = p.warmLow; // an empty range
        } else if (k == 1) {
            p.wetHigh = 0.0f; // a reversed range
        } else {
            p.bandEdge = 0.0;
        }
        try {
            const terrain::Monsoon bad(kSeed, world, p);
        } catch (const std::invalid_argument&) {
            ++refused;
        }
    }
    TEST_CHECK(refused == 3);
}

} // namespace

int main() {
    const terrain::WorldSize world;
    const terrain::Monsoon monsoon(kSeed, world);
    testDryAndCold(monsoon, world);
    testBand(monsoon, world);
    testWavyEdge(monsoon, world);
    testDeterminism(monsoon, world);
    return test::result();
}
