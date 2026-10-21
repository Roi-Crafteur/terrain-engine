// Caves of the default world (the planetary geography), at places found by search: on land with
// entrances, the caves never add rock and leave the density alone from the surface down to half
// the roof outside the reach of the entrances, their air lies between the roof and the maximum
// depth; under a deep ocean of the world nothing is carved.
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"

#include "caves_test_support.hpp"
#include "default_world_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using caves_test::Area;
using caves_test::Point;
using caves_test::Rng;
using default_world::Place;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveGround;

// Land with an entrance within 750 m (lattice of 3 km, out to 180 km).
std::optional<Place> caveSite(const CaveField& field, const CaveGround& ground) {
    return default_world::nearest(3072.0, 60, [&](double x, double z) {
        return default_world::field().heightAt(x, z) > 5.0f &&
               !field.entrancesNear(x - 750.0, z - 750.0, x + 750.0, z + 750.0, ground).empty();
    });
}

// Points above and under the surface, and around the entrances, of a square of 512 m.
void testSurface(const CaveField& field, const CaveGround& ground, const Place& site) {
    const terrain::CaveParams& params = field.params();
    const double limitDepth = static_cast<double>(params.minRoof - params.blend) / 2.0;
    Rng rng(7);
    const Area area{site.x, site.z, 256.0};
    std::vector<Point> points =
        caves_test::randomPoints(ground, rng, 1500, area, -20.0, limitDepth);
    for (const Point& p : caves_test::randomPoints(ground, rng, 500, area, -20.0, 110.0)) {
        points.push_back(p);
    }
    const std::vector<CaveEntrance> entrances = field.entrancesNear(
        area.x - area.half, area.z - area.half, area.x + area.half, area.z + area.half, ground);
    for (const CaveEntrance& e : entrances) {
        for (int k = 0; k < 150; ++k) {
            points.push_back(caves_test::pointNearEntrance(ground, rng, e, field.entranceReach(e)));
        }
    }
    terrain::CaveSampler sampler(field, ground, terrain::CaveDetail{}, caves_test::boxOf(points));
    int touched = 0;
    bool exact = true;
    bool neverAdds = true;
    for (const Point& p : points) {
        const float terrain = caves_test::terrainDensity(p.y, p.ground);
        const float carved = caves_test::carvedAt(sampler, p);
        const bool reached =
            CaveField::nearEntrance(p.x, p.z, sampler.entrances(), terrain::kEntranceReachMargin);
        const bool deeper = static_cast<double>(p.ground) - p.y > limitDepth;
        touched += carved != terrain ? 1 : 0;
        neverAdds = neverAdds && carved >= terrain;
        exact = exact && (reached || deeper || carved == terrain);
    }
    std::printf("surface: %zu points, %zu entrances, %d touched by the caves\n", points.size(),
                entrances.size(), touched);
    TEST_CHECK(!entrances.empty() && touched > 0);
    TEST_CHECK(exact);
    TEST_CHECK(neverAdds);
}

// Full columns of a square of 240 m outside the reach of the entrances: the carved air between
// the roof and the maximum depth, some of them caved.
void testDepths(const CaveField& field, const CaveGround& ground, const Place& site) {
    const terrain::CaveParams& params = field.params();
    const double roof = static_cast<double>(params.minRoof - params.blend / 4.0f);
    const double deepest = static_cast<double>(params.maxDepth + params.blend / 4.0f);
    const caves_test::Window window{site.x - 120.0,
                                    site.z - 120.0,
                                    240.0,
                                    240.0,
                                    12.0,
                                    0,
                                    static_cast<int>(params.maxDepth) + 8};
    const std::vector<CaveEntrance> entrances = field.entrancesNear(
        window.minX, window.minZ, window.minX + window.sizeX, window.minZ + window.sizeZ, ground);
    int air = 0;
    bool within = true;
    caves_test::scanColumns(field, ground, window,
                            [&](terrain::CaveSampler&, const terrain::CaveColumn& column, double y,
                                float terrain, float carved) {
                                if (CaveField::nearEntrance(column.x, column.z, entrances,
                                                            terrain::kEntranceReachMargin) ||
                                    !caves_test::carvedAir(terrain, carved)) {
                                    return;
                                }
                                const double depth = static_cast<double>(column.ground) - y;
                                ++air;
                                within = within && depth >= roof && depth <= deepest;
                            });
    std::printf("depths: %d samples of carved air\n", air);
    TEST_CHECK(air > 0);
    TEST_CHECK(within);
}

// Under a deep ocean (deeper than deepOceanDepth by 100 m), nothing is carved.
void testDeepOcean(const CaveField& field, const CaveGround& ground) {
    const float deep = -(field.params().deepOceanDepth + 100.0f);
    const std::optional<Place> ocean =
        default_world::nearest(65536.0, 300, [deep](double x, double z) {
            return default_world::field().heightAt(x, z) < deep &&
                   default_world::field().heightAt(x + 64.0, z + 64.0) < deep;
        });
    TEST_CHECK(ocean.has_value());
    if (!ocean) {
        return;
    }
    bool untouched = true;
    caves_test::scanColumns(
        field, ground,
        {ocean->x, ocean->z, 64.0, 64.0, 8.0, -10, static_cast<int>(field.params().maxDepth) + 8},
        [&untouched](terrain::CaveSampler&, const terrain::CaveColumn&, double, float terrain,
                     float carved) { untouched = untouched && carved == terrain; });
    TEST_CHECK(untouched);
}

} // namespace

int main() {
    const CaveField field(default_world::kSeed);
    const CaveGround ground = caves_test::heightfieldGround(default_world::field());
    const std::optional<Place> site = caveSite(field, ground);
    TEST_CHECK(site.has_value());
    if (site) {
        std::printf("default world: cave site at (%.1f, %.1f) km\n", site->x / 1000.0,
                    site->z / 1000.0);
        testSurface(field, ground, *site);
        testDepths(field, ground, *site);
    }
    testDeepOcean(field, ground);
    return test::result();
}
