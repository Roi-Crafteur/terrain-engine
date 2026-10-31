// The geology in data and the public magic network of the settlement (generation/settlement/
// geology.hpp, telluric.hpp) on the default world: the plates made again exactly as the
// geography makes them (the guard: if the geography changes, this test says so), the counts of
// the configuration in their ranges, the volcanoes of the arcs on the overriding plates, the same
// bits whatever the number of threads.

#include "geography_stages.hpp"
#include "settlement/geology_plates.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/settlement/geology.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/telluric.hpp"
#include "terrain/world_size.hpp"

#include "test_assert.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace st = terrain::settlement;

namespace {

constexpr std::uint64_t kSeed = 20261019; // the default world
constexpr double kCoarseCell = 65536.0;

struct World {
    terrain::WorldSize world;
    std::shared_ptr<const terrain::Geography> geo;
    st::SettlementConfig config = st::loadSettlementData(TERRAIN_SETTLEMENT_DATA).config;
    terrain::PlanetGrid grid = terrain::makePlanetGrid(world, kCoarseCell);
    std::shared_ptr<const terrain::planet::LandIndex> land;

    World() {
        const terrain::HeightfieldParams params;
        geo = terrain::Geography::shared(st::geographySeed(kSeed), params.geography, world);
        std::vector<std::uint32_t> cells;
        for (int j = 0; j < grid.nz; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                if (geo->sampleAt(grid.centerX(i), grid.centerZ(j)).coast > 0.0) {
                    cells.push_back(static_cast<std::uint32_t>(grid.index(i, j)));
                }
            }
        }
        land = std::make_shared<const terrain::planet::LandIndex>(
            terrain::planet::LandIndex::ofCells(grid.cells(), cells));
    }
};

// The guard: the tectonics made from the crust the map keeps and the seed of the Heightfield
// give the belts and the rifts of the geography, bit for bit.
void testPlatesGuard(const World& w) {
    const terrain::Geography& geo = *w.geo;
    const terrain::geography::Grid grid =
        terrain::geography::makeGrid(geo.world(), geo.params().cellSize);
    const terrain::geography::Tectonics t = terrain::geography::buildTectonics(
        st::geographySeed(kSeed), geo.params(), geo.world(), grid, st::crustOf(geo), 0);
    std::size_t differ = 0;
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            const std::size_t k = grid.index(i, j);
            differ += std::bit_cast<std::uint32_t>(t.belt[k]) !=
                      std::bit_cast<std::uint32_t>(geo.node(i, j, terrain::Geography::kBelt));
            differ += std::bit_cast<std::uint32_t>(t.trough[k]) !=
                      std::bit_cast<std::uint32_t>(geo.node(i, j, terrain::Geography::kRift));
        }
    }
    std::printf("plates made again: %zu node fields differ from the geography\n", differ);
    TEST_CHECK(differ == 0);
}

// Where the belts of the geography are strong, the nearest boundary is a collision.
void testBeltsOnCollisions(const World& w) {
    const st::GeographyPlates plates(kSeed, *w.geo, 0);
    int strong = 0;
    int collisions = 0;
    for (int j = 0; j < plates.grid().nz; ++j) {
        for (int i = 0; i < plates.grid().nx; ++i) {
            if (w.geo->node(i, j, terrain::Geography::kBelt) < 0.8f) {
                continue;
            }
            ++strong;
            const st::GeographyPlates::Near n = plates.nearAtNode(i, j);
            collisions += n.boundary >= 0 &&
                          plates.boundary(n.boundary).kind == terrain::BoundaryKind::Collision;
        }
    }
    std::printf("strong belts: %d nodes, %d on a collision\n", strong, collisions);
    TEST_CHECK(strong > 100 && collisions >= 0.9 * strong);
}

bool sameGeology(const st::Geology& a, const st::Geology& b) {
    bool same = a.volcanoes.size() == b.volcanoes.size() && a.craters.size() == b.craters.size() &&
                a.fault.values() == b.fault.values() &&
                a.majorFault.values() == b.majorFault.values() &&
                a.divergence.values() == b.divergence.values();
    for (std::size_t k = 0; same && k < a.volcanoes.size(); ++k) {
        same = a.volcanoes[k].x == b.volcanoes[k].x && a.volcanoes[k].z == b.volcanoes[k].z &&
               a.volcanoes[k].active == b.volcanoes[k].active &&
               a.volcanoes[k].large == b.volcanoes[k].large;
    }
    for (std::size_t k = 0; same && k < a.craters.size(); ++k) {
        same = a.craters[k].x == b.craters[k].x && a.craters[k].radiusKm == b.craters[k].radiusKm;
    }
    return same;
}

void testGeology(const World& w) {
    const auto start = std::chrono::steady_clock::now();
    const st::Geology g = st::buildGeology(w.grid, w.land, *w.geo, kSeed, w.config, 0);
    std::printf("geology in %.2f s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    int active = 0;
    int large = 0;
    int arcs = 0;
    int arcsOverriding = 0;
    const st::GeographyPlates plates(kSeed, *w.geo, 0);
    for (const st::Volcano& v : g.volcanoes) {
        active += v.active;
        large += v.large;
        if (v.kind != st::VolcanoKind::Arc) {
            continue;
        }
        ++arcs;
        const st::GeographyPlates::Near n = plates.nearAt(v.x, v.z);
        if (n.boundary >= 0 && plates.boundary(n.boundary).kind == terrain::BoundaryKind::Collision) {
            const int over = plates.boundary(n.boundary).overriding;
            arcsOverriding += (over == n.b ? n.towardB : -n.towardB) > 0.0;
        }
    }
    int majors = 0;
    for (const st::ArcaneCrater& c : g.craters) {
        majors += c.major;
    }
    double faults = 0.0;
    double major = 0.0;
    for (std::size_t s = 0; s < g.fault.values().size(); ++s) {
        faults += g.fault.values()[s] > 0 ? 1.0 : 0.0;
        major += g.majorFault.values()[s] > 0 ? 1.0 : 0.0;
    }
    const double landCells = static_cast<double>(g.fault.values().size());
    std::printf("volcanoes %zu (%d arcs, %d of them on the overriding plate, %d active, %d large), "
                "craters %zu (%d major), faults on %.1f %% of the land, major on %.1f %%\n",
                g.volcanoes.size(), arcs, arcsOverriding, active, large, g.craters.size(), majors,
                100.0 * faults / landCells, 100.0 * major / landCells);
    TEST_CHECK(g.volcanoes.size() >= 3000 && g.volcanoes.size() <= 6000);
    TEST_CHECK(arcs > static_cast<int>(g.volcanoes.size()) / 2);
    TEST_CHECK(arcsOverriding >= 0.9 * arcs);
    TEST_CHECK(active >= 300 && active <= 800 && large >= 50 && large <= 150);
    TEST_CHECK(g.craters.size() >= 30 && g.craters.size() <= 80 && majors >= 5 && majors <= 15);
    TEST_CHECK(faults > 0.02 * landCells && faults < 0.5 * landCells && major > 0);
    TEST_CHECK(sameGeology(g, st::buildGeology(w.grid, w.land, *w.geo, kSeed, w.config, 1)));
    TEST_CHECK(sameGeology(g, st::buildGeology(w.grid, w.land, *w.geo, kSeed, w.config, 3)));
}

void testTelluric(const World& w) {
    const double scale = st::worldScale(w.grid, w.config);
    const st::TelluricNetwork n = st::buildTelluricNetwork(w.world, kSeed, w.config.magic, scale);
    int majors = 0;
    bool continuous = true;
    bool canonical = true;
    for (const st::TelluricLine& l : n.lines) {
        majors += l.major;
        canonical = canonical && std::abs(l.x[0]) <= 0.5 * w.world.x &&
                    std::abs(l.z[0]) <= 0.5 * w.world.z;
        for (std::size_t p = 1; p < l.x.size(); ++p) {
            const double step = std::hypot(l.x[p] - l.x[p - 1], l.z[p] - l.z[p - 1]);
            continuous = continuous && std::abs(step - st::kLineStepKm * 1000.0) < 1.0;
        }
    }
    int crossings = 0;
    for (const st::ManaNode& node : n.nodes) {
        crossings += node.crossing;
    }
    const int minors = static_cast<int>(n.lines.size()) - majors;
    std::printf("telluric lines: %d major, %d minor; nodes %zu (%d at crossings); dead zones %zu\n",
                majors, minors, n.nodes.size(), crossings, n.deadZones.size());
    TEST_CHECK(majors >= 10 && majors <= 30 && minors >= 40 && minors <= 90);
    TEST_CHECK(n.nodes.size() >= 420 && n.nodes.size() <= 1050 && crossings > 0);
    TEST_CHECK(n.deadZones.size() >= 42 && n.deadZones.size() <= 105);
    TEST_CHECK(continuous && canonical);
    const st::TelluricNetwork again = st::buildTelluricNetwork(w.world, kSeed, w.config.magic, scale);
    TEST_CHECK(again.nodes.size() == n.nodes.size() && again.lines.size() == n.lines.size() &&
               again.nodes.back().x == n.nodes.back().x && again.lines[3].z == n.lines[3].z);
    const st::TelluricNetwork other =
        st::buildTelluricNetwork(w.world, kSeed + 1, w.config.magic, scale);
    TEST_CHECK(other.lines[0].x != n.lines[0].x);
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    const World w;
    std::printf("default geography in %.2f s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    testPlatesGuard(w);
    testBeltsOnCollisions(w);
    testGeology(w);
    testTelluric(w);
    return test::result();
}
