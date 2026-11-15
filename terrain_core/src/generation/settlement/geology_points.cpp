#include "geology_points.hpp"

#include "settlement_random.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace terrain::settlement {

namespace {

// Attempts to place a point at its spacing before giving it up.
constexpr int kAttempts = 24;

} // namespace

std::vector<ArcaneCrater> placeCraters(const WorldSize& world, std::uint64_t worldSeed,
                                       const GeologyRule& rule, double scale) {
    const int count = drawCount(worldSeed, "arcaneCraters", rule.arcaneCraters, scale);
    const int majors =
        std::min(count, drawCount(worldSeed, "majorCraters", rule.majorCraters, scale));
    PointSpacing spacing(2000.0 * rule.majorCraterRadiusKm.max, world.x, world.z);
    const std::uint64_t stream = streamOf(worldSeed, "craterPositions");
    std::uint64_t draw = 0;
    std::vector<ArcaneCrater> craters;
    for (int c = 0; c < count; ++c) {
        const bool major = c < majors;
        const double radius = drawIn(stream, draw++, major ? rule.majorCraterRadiusKm
                                                           : rule.craterRadiusKm);
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            const double x = (unitDraw(stream, draw++) - 0.5) * world.x;
            const double z = (unitDraw(stream, draw++) - 0.5) * world.z;
            if (spacing.farEnough(x, z)) {
                spacing.add(x, z);
                craters.push_back(ArcaneCrater{x, z, radius, major});
                break;
            }
        }
    }
    return craters;
}

} // namespace terrain::settlement
