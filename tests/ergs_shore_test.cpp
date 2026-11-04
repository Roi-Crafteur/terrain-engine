// The edges of the seas of dunes along the water (agent 34, ergs.hpp):
// - the floor of a valley is found whatever the limit of the search (small world, its
//   hydrology): a wide floor lies farther from the line of its reach than the limit, and
//   Hydrology::floorDistance must still find it, or the mask jumps along a straight line;
// - across a straight floor 6 km wide laid through a full erg (the default world, a coarse
//   planet), the mask moves by little between points 5 m apart (no cut), no dune stands within
//   riverClear of the floor, and the edge of the dunes wanders along it: never a straight band.
#include "dunes_support.hpp"
#include "test_assert.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

void testFloorWhateverTheLimit() {
    terrain::HydrologyParams p;
    p.ergs = dunes_test::smallErgParams();
    const terrain::Hydrology h(dunes_test::kSeed, dunes_test::small().relief,
                               dunes_test::small().planet, p);
    std::size_t points = 0;
    std::size_t onFloor = 0;
    std::size_t differ = 0;
    // Around the cells of 40 carved rivers: across their floors and beyond (6 km on each side).
    const terrain::PlanetLevel& planet = *dunes_test::small().planet;
    const terrain::PlanetGrid& g = planet.grid();
    std::size_t rivers = 0;
    for (std::size_t c = 0; c < g.cells() && rivers < 40; c += 7) {
        const terrain::PlanetCells::RiverCell* river = planet.cells().riverAt(c);
        if (river == nullptr ||
            planet.rivers().segments[static_cast<std::size_t>(river->segment)]
                    .vertices[static_cast<std::size_t>(river->vertex)]
                    .discharge < 10.0f) {
            continue;
        }
        ++rivers;
        for (int j = -6; j <= 6; ++j) {
            for (int i = -6; i <= 6; ++i) {
                const double x = g.centerX(g.column(c)) + 1'000.0 * i;
                const double z = g.centerZ(g.row(c)) + 1'000.0 * j;
                const double near = h.floorDistance(x, z, 1'150.0);
                const double far = std::min(h.floorDistance(x, z, 40'000.0), 1'150.0);
                ++points;
                onFloor += far == 0.0 ? 1 : 0;
                differ += near == far ? 0 : 1;
            }
        }
    }
    std::printf("floor: %zu points (%zu on a floor), %zu differ with the limit of the search\n",
                points, onFloor, differ);
    TEST_CHECK(onFloor > 0);
    TEST_CHECK(differ == 0);
}

constexpr double kFloorHalf = 3'000.0;

// A full point of an erg of the default world (lattice of 64 km).
bool fullSite(double& x0, double& z0) {
    const terrain::Ergs& ergs = dunes_test::real().ergs;
    for (double z = 2'000'000.0; z < 4'000'000.0; z += 65'536.0) {
        for (double x = -3'000'000.0; x < 3'000'000.0; x += 65'536.0) {
            if (ergs.at(x, z, 0.0f).mask > 0.8f) {
                x0 = x;
                z0 = z;
                return true;
            }
        }
    }
    return false;
}

void testStraightFloor() {
    double x0 = 0.0;
    double z0 = 0.0;
    TEST_CHECK(fullSite(x0, z0));
    // The floor: the band |z - z0| < kFloorHalf, straight along x.
    const terrain::Ergs ergs(dunes_test::kSeed, *dunes_test::real().planet, {}, terrain::ErgParams{},
                             [z0](double, double z, double limit) {
                                 return std::min(limit, std::max(0.0, std::abs(z - z0) - kFloorHalf));
                             });
    const terrain::ErgParams params;
    double step = 0.0;
    std::size_t near = 0;
    std::vector<double> edge; // where the mask first passes 0.5 north of the floor, per column
    for (int c = 0; c < 40; ++c) {
        const double x = x0 + 500.0 * c;
        float last = -1.0f;
        double first = -1.0;
        for (int k = 0; k < 800; ++k) {
            const double d = 5.0 * k; // from the edge of the floor
            const float mask = ergs.at(x, z0 + kFloorHalf + d, 0.0f).mask;
            if (last >= 0.0f) {
                step = std::max(step, static_cast<double>(std::abs(mask - last)));
            }
            near += d < params.riverClear && mask > 0.0f ? 1 : 0;
            if (first < 0.0 && mask > 0.5f * ergs.at(x, z0 + kFloorHalf + 3'000.0, 0.0f).mask) {
                first = d;
            }
            last = mask;
        }
        edge.push_back(first);
    }
    const auto [lo, hi] = std::minmax_element(edge.begin(), edge.end());
    std::printf("straight floor: steepest step of the mask %.4f per 5 m, %zu points with dunes "
                "within %.0f m, the edge from %.0f to %.0f m off the floor along 20 km\n",
                step, near, params.riverClear, *lo, *hi);
    TEST_CHECK(step <= 0.05);
    TEST_CHECK(near == 0);
    TEST_CHECK(*lo >= 0.0 && *hi - *lo >= 400.0); // it wanders
}

} // namespace

int main() {
    testFloorWhateverTheLimit();
    testStraightFloor();
    return test::result();
}
