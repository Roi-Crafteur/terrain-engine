// The sunken sedimentary basins of the geography (agent 52, GeographyParams::sunkenBasins), on
// the default world, on and off over a grid of the world:
// - only the regional elevation of the land changes: the coast, the floor of the sea, the belts,
//   the intensity and the character of the relief, the rifts keep every bit;
// - it only goes down, never within sunkCoast[0] of the sea (a sill; at the scale of the nodes of
//   the map), and some of it below the sea level: closed basins at the scale of the map, deep
//   inland, 0.05 to 0.3 % of the land (the target of the audit for every low: 0.05 to 0.5 %).
#include "terrain/generation/geography.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace {

using terrain::Geography;
using terrain::GeographySample;

std::shared_ptr<const Geography> geographyWith(bool sunken) {
    terrain::GeographyParams p;
    p.sunkenBasins = sunken;
    return Geography::shared(terrain::kDefaultWorldSeed, p, terrain::WorldSize{});
}

bool sameButLand(const GeographySample& a, const GeographySample& b) {
    return bit_exact::same(a.coast, b.coast) && bit_exact::same(a.sea, b.sea) &&
           bit_exact::same(a.belt, b.belt) && bit_exact::same(a.relief, b.relief) &&
           bit_exact::same(a.character, b.character) && bit_exact::same(a.rift, b.rift) &&
           bit_exact::same(a.riftFloor, b.riftFloor);
}

void testSunken() {
    const std::shared_ptr<const Geography> off = geographyWith(false);
    const std::shared_ptr<const Geography> on = geographyWith(true);
    const terrain::GeographyParams params;
    const terrain::WorldSize w;
    // The distance to the coast of the map at a point and at the nodes around it differ by up to
    // about two nodes (65 km apart).
    constexpr double kNode = 150'000.0;
    constexpr int kNx = 600;
    constexpr int kNz = 350;
    std::size_t land = 0;
    std::size_t lowered = 0;
    std::size_t below = 0;
    std::size_t raised = 0;
    std::size_t nearSea = 0;
    std::size_t other = 0;
    float deepest = 0.0f;
    double nearest = 1e30; // the distance to the coast of the map of the nearest point changed
    for (int j = 0; j < kNz; ++j) {
        for (int i = 0; i < kNx; ++i) {
            const double x = (i + 0.5) * w.x / kNx - 0.5 * w.x;
            const double z = (j + 0.5) * w.z / kNz - 0.5 * w.z;
            const GeographySample a = on->sampleAt(x, z);
            const GeographySample b = off->sampleAt(x, z);
            other += sameButLand(a, b) ? 0 : 1;
            if (!(b.coast > 0.0)) {
                continue;
            }
            ++land;
            raised += a.land > b.land ? 1 : 0;
            lowered += a.land < b.land ? 1 : 0;
            // A point kNode nearer the sea than the sill keeps its bits.
            nearSea += static_cast<double>(b.mapCoast) < params.sunkCoast[0] - kNode &&
                               !bit_exact::same(a.land, b.land)
                           ? 1
                           : 0;
            if (!bit_exact::same(a.land, b.land)) {
                nearest = std::min(nearest, static_cast<double>(b.mapCoast));
            }
            below += a.land < 0.0f && !(a.rift > 0.0f) ? 1 : 0;
            deepest = std::min(deepest, a.land);
        }
    }
    std::printf("of %zu land points: %zu lowered (%.2f %%), %zu below the sea level (%.3f %%), "
                "deepest %.0f m; %zu raised, %zu changed near the sea (the nearest %.0f km from it), "
                "%zu other fields changed\n",
                land, lowered, 100.0 * static_cast<double>(lowered) / static_cast<double>(land),
                below, 100.0 * static_cast<double>(below) / static_cast<double>(land),
                static_cast<double>(deepest), raised, nearSea, nearest / 1000.0, other);
    TEST_CHECK(other == 0 && raised == 0 && nearSea == 0);
    TEST_CHECK(lowered > land / 100);
    TEST_CHECK(below * 2000 > land && below * 333 < land);
}

} // namespace

int main() {
    testSunken();
    return test::result();
}
