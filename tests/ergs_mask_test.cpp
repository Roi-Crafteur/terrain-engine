// The mask of the seas of dunes (agent 34, ergs.hpp) on the small world: ergs there are (in the
// dry heart of its continents), never under a humid climate, on frozen ground, under the
// mountains nor on the sea of the relief; never in a lake cell nor on the course of a river carved; their edges
// progressive (no step); the climate they read joins smoothly across the sharp lines of the
// climate of the planet level; disabled, none; the same bits for the same seed.
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/planet/planet_climate.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

using dunes_test::small;

// The humidity of a humid climate (P above PET): never an erg there.
constexpr float kHumid = 0.55f;

template <class F> void eachPoint(double step, F&& f) {
    const terrain::PlanetGrid& g = small().planet->grid();
    for (double z = -0.5 * g.periodZ() + 0.5 * step; z < 0.5 * g.periodZ(); z += step) {
        for (double x = -0.5 * g.periodX() + 0.5 * step; x < 0.5 * g.periodX(); x += step) {
            f(x, z);
        }
    }
}

void testWhere() {
    std::size_t land = 0;
    std::size_t ergs = 0;
    std::size_t wrong = 0;
    float wettest = 0.0f;
    eachPoint(4096.0, [&](double x, double z) {
        const terrain::HeightSample s = small().relief.sampleAt(x, z);
        const terrain::PlanetSample p = small().planet->sampleAt(x, z);
        const float mask = s.water == terrain::WaterKind::Ocean
                               ? 0.0f
                               : small().ergs.at(x, z, s.relief).mask;
        land += s.water == terrain::WaterKind::Ocean ? 0 : 1;
        if (!(mask > 0.0f)) {
            return;
        }
        ++ergs;
        const float humidity = small().planet->humidityAt(x, z);
        wettest = std::max(wettest, humidity);
        const bool frozen = p.temperature < small().planet->params().climate.iceTemperature;
        // The sea is that of the relief (the cells of the planet level only follow the coast
        // to within one cell): s.water is never Ocean here.
        const bool bad = humidity >= kHumid || frozen || s.relief >= 0.28f;
        wrong += bad ? 1 : 0;
    });
    std::printf("ergs: %zu of %zu points of land (%.2f %%), the wettest at humidity %.2f\n", ergs,
                land, 100.0 * static_cast<double>(ergs) / static_cast<double>(land), wettest);
    TEST_CHECK(ergs > 50);
    TEST_CHECK(wrong == 0);
}

// No erg on a lake cell nor within the clear corridor of the course of a river carved.
void testWater() {
    const terrain::PlanetLevel& planet = *small().planet;
    const terrain::PlanetGrid& g = planet.grid();
    std::size_t wet = 0;
    std::size_t wrong = 0;
    for (std::size_t c = 0; c < g.cells(); ++c) {
        const terrain::PlanetCells& cells = planet.cells();
        const bool lake = cells.lakeAt(c) >= 0;
        const terrain::PlanetCells::RiverCell* river = cells.riverAt(c);
        const bool carved =
            river != nullptr &&
            planet.rivers().segments[static_cast<std::size_t>(river->segment)]
                    .vertices[static_cast<std::size_t>(river->vertex)]
                    .discharge >= 10.0f;
        if (!lake && !carved) {
            continue;
        }
        const double cx = g.centerX(g.column(c));
        const double cz = g.centerZ(g.row(c));
        // The center (on the course of a river) and, for a lake, its whole square.
        for (int k = 0; k < (lake ? 25 : 1); ++k) {
            const double x = cx + (lake ? (k % 5 - 2) * 0.24 * g.cellSize : 0.0);
            const double z = cz + (lake ? (k / 5 - 2) * 0.24 * g.cellSize : 0.0);
            ++wet;
            wrong += small().ergs.at(x, z, 0.0f).mask > 0.0f ? 1 : 0;
        }
    }
    std::printf("water: %zu points of lakes and rivers, %zu under an erg\n", wet, wrong);
    TEST_CHECK(wet > 100);
    TEST_CHECK(wrong == 0);
}

// No step: the mask moves by little between points 250 m apart, and the climate it reads moves by
// little between points one cell (8 km) apart, even where the humidity of the planet level jumps.
void testEdges() {
    double maskStep = 0.0;
    double climateStep = 0.0;
    double humidityStep = 0.0;
    eachPoint(8192.0, [&](double x, double z) {
        const float a = small().ergs.at(x, z, 0.0f).mask;
        const float b = small().ergs.at(x + 250.0, z + 250.0, 0.0f).mask;
        maskStep = std::max(maskStep, static_cast<double>(std::abs(a - b)));
        climateStep = std::max(climateStep, static_cast<double>(std::abs(
                                                small().ergs.climateAt(x, z + 8192.0) -
                                                small().ergs.climateAt(x, z))));
        humidityStep = std::max(humidityStep, static_cast<double>(std::abs(
                                                  small().planet->humidityAt(x, z + 8192.0) -
                                                  small().planet->humidityAt(x, z))));
    });
    std::printf("edges: mask step %.3f per 250 m; per cell along z: climate %.3f, humidity of "
                "the planet %.3f\n",
                maskStep, climateStep, humidityStep);
    TEST_CHECK(maskStep <= 0.2);
    TEST_CHECK(climateStep <= 0.25);
    TEST_CHECK(climateStep < humidityStep);
}

void testDisabledAndDeterministic() {
    terrain::ErgParams off = dunes_test::smallErgParams();
    off.enabled = false;
    const terrain::Ergs none(dunes_test::kSeed, *small().planet, small().relief.depressions(), off);
    const terrain::Ergs twin(dunes_test::kSeed, *small().planet, small().relief.depressions(),
                             dunes_test::smallErgParams());
    bool nothing = true;
    bool same = true;
    eachPoint(16384.0, [&](double x, double z) {
        nothing = nothing && none.at(x, z, 0.0f).mask == 0.0f && !none.mayHold(x, z);
        const terrain::ErgPoint a = small().ergs.at(x, z, 0.0f);
        const terrain::ErgPoint b = twin.at(x, z, 0.0f);
        same = same && std::bit_cast<std::uint32_t>(a.mask) == std::bit_cast<std::uint32_t>(b.mask);
        if (a.mask > 0.0f) {
            same = same && std::bit_cast<std::uint32_t>(small().ergs.dunesOf(x, z, a)) ==
                               std::bit_cast<std::uint32_t>(twin.dunesOf(x, z, b));
        }
    });
    TEST_CHECK(nothing);
    TEST_CHECK(same);
}

} // namespace

int main() {
    testWhere();
    testWater();
    testEdges();
    testDisabledAndDeterministic();
    return test::result();
}
