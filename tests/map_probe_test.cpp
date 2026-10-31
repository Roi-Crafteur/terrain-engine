// The probe of the dev map (map_probe.hpp): the ground and the water of the heightfield bit for
// bit, the biome as the far horizon classifies its ground (Ecology::map of surfaceFromHeightfield
// every 8 m, the oracle of horizon_sampler_test), and the values of the planet level with one.
#include "terrain/map/map_probe.hpp"

#include "map_world_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <bit>
#include <cstdint>

using namespace terrain;

namespace {

bool sameBits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// The probe at sample (1, 1) of a region of 4 x 4 samples 8 m apart centered on (cx, cz): its
// biome is the one Ecology::map gives that sample.
bool matchesOracle(const MapWorldSource& source, const Heightfield& field, const Ecology& ecology,
                   double cx, double cz) {
    const HeightmapRegion region{cx, cz, kMapProbeSlopeStep, 4};
    const TerrainSurface surface = surfaceFromHeightfield(field, region);
    const EcologyMap map = ecology.map(surface);
    const MapProbe p = probeAt(source, surface.sampleX(1), surface.sampleZ(1));
    const HeightSample h = field.sampleAt(surface.sampleX(1), surface.sampleZ(1));
    const float level = h.water == WaterKind::None ? kNoWaterLevel : h.waterLevel;
    return sameBits(p.ground, h.height) && p.water == h.water && sameBits(p.waterLevel, level) &&
           p.biome == map.at(1, 1).biome;
}

void testDry() {
    const map_world::DryWorld& w = map_world::dry();
    bool all = true;
    // The beach of the origin, the K2, a plain, the open sea, across the east edge.
    for (const auto& [x, z] : {std::pair{-3985.0, -165.0}, std::pair{416857.0, -11405966.0},
                               std::pair{-241631.0, -86277.0}, std::pair{-6184762.0, 155681.0},
                               std::pair{30015480.0, 1000.0}}) {
        all = all && matchesOracle(w.source, w.field, w.ecology, x, z);
    }
    TEST_CHECK(all);
    const MapProbe p = probeAt(w.source, -3985.0, -165.0);
    TEST_CHECK(!p.planet && p.lake == -1 && p.discharge == 0.0f);
    // Any image: the same probe whole worlds away.
    const MapProbe q = probeAt(w.source, -3985.0 + 2.0 * WorldSize{}.x, -165.0 - WorldSize{}.z);
    TEST_CHECK(sameBits(q.ground, p.ground) && q.biome == p.biome && q.x == p.x && q.z == p.z);
}

// On a river of the small world: the river at its level, the discharge of its planet cell.
void testWet() {
    const map_world::WetWorld w;
    TEST_CHECK(matchesOracle(w.source, w.tiles.heightfield(), w.tiles.ecology(), 1000.0, 2000.0));
    const RiverReach reach = w.hydrology->reach(0);
    const RiverPoint& mid = reach.points[reach.points.size() / 2];
    const MapProbe p = probeAt(w.source, mid.x, mid.z);
    const HeightSample h = w.tiles.heightfield().sampleAt(mid.x, mid.z);
    TEST_CHECK(p.planet);
    TEST_CHECK(p.water == h.water && sameBits(p.ground, h.height));
    TEST_CHECK(p.discharge == w.planet->sampleAt(mid.x, mid.z).discharge);
    TEST_CHECK(p.water == WaterKind::None ? p.surface() == p.ground : p.surface() == p.waterLevel);
    std::size_t wet = 0;
    for (std::size_t k = 0; k < reach.points.size(); k += 8) {
        wet += probeAt(w.source, reach.points[k].x, reach.points[k].z).water == WaterKind::River ? 1u
                                                                                               : 0u;
    }
    TEST_CHECK(wet > 0);
}

} // namespace

int main() {
    testDry();
    testWet();
    return test::result();
}
