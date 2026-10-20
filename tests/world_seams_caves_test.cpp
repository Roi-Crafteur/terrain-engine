// The grids of the world and the caves across the edges of the wrapped world (WorldSize). Every
// grid has round(period / nominal size) cells per side, of the effective size period / count (the
// table printed below is the list of the grids of Phase 9), and its cell of canonical index i is
// the cell i + count: the aquifer cells, the districts and the nodes of the cave environment hold
// the same values there, their sites moved by a whole world. The carved density, the columns and
// the environment give the same bits at every image of a point, and the carved density and the
// environment do not jump across the edges. The lattices of the noises: noise_periodic_test.cpp.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/arches.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"
#include "world_seams_support.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using seams_test::kWorld;
using seams_test::Point;
using terrain::PeriodicCells;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

terrain::CaveParams cavesWithDistricts() {
    terrain::CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    return params;
}

struct Grid {
    const char* name;
    double nominal;
    const PeriodicCells& x;
    const PeriodicCells& z;
};

// The grids of the world follow the rule; a point and its images lie in the same canonical
// cell.
void testGrids(const terrain::TerrainTiles& tiles) {
    const terrain::CaveField& caves = *tiles.caves();
    const terrain::CaveParams& p = caves.params();
    const terrain::CaveEnvironmentField environment(tiles.heightfield(), tiles.ecology().climate());
    const terrain::SpeleothemField speleothems(kSeed, p.speleothems);
    const terrain::CaveFloraField flora(kSeed, p.districts->flora);
    const terrain::ArchField arches(kSeed);
    const terrain::VegetationField& plants = tiles.ecology().vegetation();
    const Grid grids[] = {
        {"cave noise nodes", terrain::kCaveNodeMeters, caves.lattice().nodesX(),
         caves.lattice().nodesZ()},
        {"cave environment nodes", terrain::kCaveEnvironmentSpacing, environment.nodesX(),
         environment.nodesZ()},
        {"gallery level nodes", p.galleries.levelSpacing, caves.galleryNodesX(),
         caves.galleryNodesZ()},
        {"entrance cells", p.entrances.spacing, caves.entranceCellsX(), caves.entranceCellsZ()},
        {"aquifer cells", p.aquifers.cellSize, caves.aquiferCellsX(), caves.aquiferCellsZ()},
        {"districts", p.districts->cellSize, caves.districts()->cellsX(),
         caves.districts()->cellsZ()},
        {"speleothem cells", p.speleothems.spacing, speleothems.cellsX(), speleothems.cellsZ()},
        {"cave flora cells", p.districts->flora.spacing, flora.cellsX(), flora.cellsZ()},
        {"plant cells", tiles.ecology().params().vegetation.cellSize, plants.cellsX(),
         plants.cellsZ()},
        {"arch cells", arches.params().spacing, arches.cellsX(), arches.cellsZ()},
    };
    const std::vector<Point> points = seams_test::testPoints(60, 3);
    for (const Grid& g : grids) {
        bool rule = seams_test::followsTheRule(g.x, kWorld.x, g.nominal) &&
                    seams_test::followsTheRule(g.z, kWorld.z, g.nominal);
        for (const Point q : points) {
            for (const auto& k : seams_test::kImages) {
                const Point image = seams_test::imageOf(q, k);
                rule = rule && g.x.wrap(g.x.cellOf(image.x)) == g.x.wrap(g.x.cellOf(q.x)) &&
                       g.z.wrap(g.z.cellOf(image.z)) == g.z.wrap(g.z.cellOf(q.z));
            }
        }
        std::printf("%-22s %7.1f m: %9lld x %9lld cells of %.9g x %.9g m\n", g.name, g.nominal,
                    static_cast<long long>(g.x.count), static_cast<long long>(g.z.count), g.x.size,
                    g.z.size);
        TEST_CHECK(rule);
    }
}

// The same cell at another image: the same values, its site moved by whole worlds (exactly but
// for the rounding of the larger coordinates).
template <typename Cell>
bool sameCell(Cell a, Cell b) {
    const bool site = std::abs(terrain::wrapCoordinate(a.siteX, kWorld.x) -
                               terrain::wrapCoordinate(b.siteX, kWorld.x)) < 1e-6 &&
                      std::abs(terrain::wrapCoordinate(a.siteZ, kWorld.z) -
                               terrain::wrapCoordinate(b.siteZ, kWorld.z)) < 1e-6;
    b.cellX = a.cellX;
    b.cellZ = a.cellZ;
    b.siteX = a.siteX;
    b.siteZ = a.siteZ;
    return site && a == b;
}

// The aquifer cells and the districts around the edge x = world.x / 2 and around the corner,
// given at three images of their index; the nodes of the cave environment likewise.
void testCellsAtImages(const terrain::TerrainTiles& tiles) {
    const terrain::CaveField& caves = *tiles.caves();
    const terrain::CaveGround& ground = tiles.caveGround();
    const terrain::CaveEnvironmentField environment(tiles.heightfield(), tiles.ecology().climate());
    const auto count = [](const PeriodicCells& cells) { return static_cast<int>(cells.count); };
    const int ax = count(caves.aquiferCellsX());
    const int az = count(caves.aquiferCellsZ());
    const int dx = count(caves.districts()->cellsX());
    const int dz = count(caves.districts()->cellsZ());
    const int ex = count(environment.nodesX());
    const int ez = count(environment.nodesZ());
    bool aquifers = true;
    bool districts = true;
    bool nodes = true;
    for (int j = -2; j < 2; ++j) {
        for (int i = -2; i < 2; ++i) {
            for (const int row : {j - 53000, j + az / 2}) {
                const terrain::AquiferCell a = caves.aquifer(ax / 2 + i, row, ground);
                aquifers = aquifers && sameCell(a, caves.aquifer(ax / 2 + i - ax, row, ground)) &&
                           sameCell(a, caves.aquifer(ax / 2 + i + ax, row - az, ground));
            }
            for (const int row : {j - 3000, j + dz / 2}) {
                const terrain::CaveDistrict d = caves.district(dx / 2 + i, row, ground);
                districts = districts &&
                            sameCell(d, caves.district(dx / 2 + i - dx, row, ground)) &&
                            sameCell(d, caves.district(dx / 2 + i + dx, row + 2 * dz, ground));
            }
            const terrain::CaveEnvironment e = environment.node(ex / 2 + i, ez / 2 + j);
            nodes = nodes && e == environment.node(ex / 2 + i - ex, ez / 2 + j) &&
                    e == environment.node(ex / 2 + i + ex, ez / 2 + j - 3 * ez);
        }
    }
    TEST_CHECK(aquifers && districts && nodes);
}

std::vector<float> columnOf(const terrain::CaveField& caves, const terrain::CaveGround& ground,
                            double x, double z) {
    const float height = ground.height(x, z);
    terrain::AquiferCache cells(caves, ground);
    const terrain::CaveColumn c = caves.column(x, z, height, ground, {}, &cells);
    std::vector<float> values = {c.ground,
                                 c.exclusion,
                                 c.ravineDistance,
                                 c.ravineStrength,
                                 c.waterLevel,
                                 c.galleryDistance,
                                 c.galleryFloor,
                                 c.riverLevel,
                                 c.lavaLevel,
                                 static_cast<float>(c.entranceCount),
                                 static_cast<float>(c.barrierCount)};
    values.insert(values.end(), c.profile.values.begin(), c.profile.values.end());
    return values;
}

// The columns, the carved density 9.5 and 44 m under the ground and the environment: bit for bit at
// every image of a point; the carved density and the environment continuous across the edges.
void testCavesAtImages(const terrain::TerrainTiles& tiles) {
    const terrain::CaveField& caves = *tiles.caves();
    const terrain::CaveGround& ground = tiles.caveGround();
    const terrain::CaveEnvironmentField environment(tiles.heightfield(), tiles.ecology().climate());
    const terrain::CaveDetail detail = terrain::caveDetailForLod(0);
    const auto density = [&](double x, double z, double depth) {
        return terrain::caveDensityAt(caves, ground, detail, x, ground.height(x, z) - depth, z);
    };
    bool same = true;
    std::size_t carved = 0;
    for (const Point p : seams_test::testPoints(36, 4)) {
        const std::vector<float> column = columnOf(caves, ground, p.x, p.z);
        const terrain::CaveEnvironment e = environment.at(p.x, p.z);
        for (const double depth : {9.5, 44.0}) {
            const float d = density(p.x, p.z, depth);
            carved += d > -static_cast<float>(depth) ? 1 : 0;
            for (const auto& k : seams_test::kImages) {
                const Point q = seams_test::imageOf(p, k);
                same = same && density(q.x, q.z, depth) == d;
            }
        }
        for (const auto& k : seams_test::kImages) {
            const Point q = seams_test::imageOf(p, k);
            same = same && columnOf(caves, ground, q.x, q.z) == column &&
                   environment.at(q.x, q.z) == e;
        }
    }
    std::printf("caves: columns, densities and environments the same at every image of 36 "
                "points (%zu samples carved)\n",
                carved);
    TEST_CHECK(same && carved > 0);
    for (const seams_test::Walk& walk : seams_test::kWalks) {
        TEST_CHECK(seams_test::continuousAcross(
            walk,
            [&](double x, double z) {
                const terrain::CaveEnvironment e = environment.at(x, z);
                return std::vector<float>{density(x, z, 12.0), density(x, z, 37.0), e.temperature,
                                          e.humidity};
            },
            "caves and environment", 25));
    }
}

} // namespace

int main() {
    const terrain::TerrainTiles tiles(kSeed, {}, {}, cavesWithDistricts());
    testGrids(tiles);
    testCellsAtImages(tiles);
    testCavesAtImages(tiles);
    return test::result();
}
