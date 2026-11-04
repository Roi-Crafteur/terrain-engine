// The cost of the seas (agent 51, seas.hpp) on the small world, the same code with the seas on and
// off, measured pair by pair (cost_ratio_support.hpp): at most 1.3 times on the land, where the
// seas only look at the water of a sample; reported at sea (the open sea, a sloping margin, a
// deep floor), held under a loose bound against a runaway; and the bytes the seas hold.

#include "cost_ratio_support.hpp"
#include "seas_support.hpp"
#include "test_assert.hpp"

#include <cstdio>
#include <vector>

namespace {

// A place at sea or on land of the small world at least 60 km from the coast signal, from a
// lattice of 64 km, the first met.
std::pair<double, double> placeOf(bool land, int skip) {
    for (double z = -0.5 * golden::kSmallPlanet.z; z < 0.5 * golden::kSmallPlanet.z;
         z += 65'536.0) {
        for (double x = -0.5 * golden::kSmallPlanet.x; x < 0.5 * golden::kSmallPlanet.x;
             x += 65'536.0) {
            const double coast = seas_test::coastOf(dunes_test::small().relief.sampleAt(x, z));
            if ((land ? coast > 60'000.0 : coast < -60'000.0) && skip-- <= 0) {
                return {x, z};
            }
        }
    }
    return {0.0, 0.0};
}

cost_test::Ratio rowsAt(std::pair<double, double> at, double most, const char* where) {
    const seas_test::Worlds& w = seas_test::worlds();
    const cost_test::TimedRun run = [&](cost_test::Side side, std::size_t item) {
        const terrain::TerrainTiles& t = side == cost_test::Side::With ? w.withSeas : w.without;
        std::vector<double> xs;
        std::vector<double> zs;
        for (int i = 0; i < 4096; ++i) {
            xs.push_back(at.first + 4.0 * i);
            zs.push_back(at.second + 50.0 * static_cast<double>(item));
        }
        std::vector<terrain::HeightSample> out(xs.size());
        return cost_test::secondsOf([&] { t.heightfield().samplesAt(xs, zs, out); });
    };
    const cost_test::Ratio ratio = cost_test::ratioAtMost(6, run, most);
    std::printf("cost %-22s %s\n", where, cost_test::ratioText(ratio).c_str());
    return ratio;
}

cost_test::Ratio tilesAt(std::pair<double, double> at, int lod, double most, const char* where) {
    const seas_test::Worlds& w = seas_test::worlds();
    const cost_test::TimedRun run = [&](cost_test::Side side, std::size_t item) {
        const terrain::TerrainTiles& t = side == cost_test::Side::With ? w.withSeas : w.without;
        const double x = at.first + 64.0 * static_cast<double>(item);
        return cost_test::secondsOf([&] { (void)seas_test::tileAt(t, x, at.second, lod); });
    };
    const cost_test::Ratio ratio = cost_test::ratioAtMost(4, run, most);
    std::printf("cost %-22s %s\n", where, cost_test::ratioText(ratio).c_str());
    return ratio;
}

} // namespace

int main() {
    const auto land = placeOf(true, 0);
    const auto sea = placeOf(false, 0);
    const auto deep = placeOf(false, 40);
    TEST_CHECK(rowsAt(land, 1.3, "rows on land").passed());
    TEST_CHECK(tilesAt(land, 0, 1.3, "tiles LOD 0 on land").passed());
    TEST_CHECK(tilesAt(land, 2, 1.3, "tiles LOD 2 on land").passed());
    // At sea, reported: the seas there are the work of the sample.
    TEST_CHECK(rowsAt(sea, 4.0, "rows at sea").passed());
    TEST_CHECK(rowsAt(deep, 4.0, "rows at sea, farther").passed());
    TEST_CHECK(tilesAt(sea, 0, 4.0, "tiles LOD 0 at sea").passed());
    const terrain::Seas* seas = seas_test::worlds().on->seas();
    TEST_CHECK(seas != nullptr);
    if (seas != nullptr) {
        std::printf("the seas of the small world hold %zu bytes\n", seas->memoryBytes());
    }
    return test::result();
}
