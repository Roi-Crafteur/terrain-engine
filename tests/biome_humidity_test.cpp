// The humidity the biomes read from the planet level (agent 49, ClimateParams::humidityWarp): its
// cells of the small planet are read bilinearly, so its contours fold along the lines of the
// centers of the cells and the borders of the biomes followed the polylines of the grid. Read at
// a warped point, the folds no longer lie on those lines.
// - along transects in x and in z, the bends of the humidity (the second differences over a few
//   hundred meters) next to the lines of the cell centers against those between them: far
//   stronger on the lines without the warp (the test sees the folds), no stronger with it;
// - the warp is deterministic and stays within humidityWarp meters (the same humidity as the
//   source read there).
#include "terrain/generation/climate.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

class LevelHumidity final : public terrain::HumiditySource {
public:
    explicit LevelHumidity(std::shared_ptr<const terrain::PlanetLevel> level)
        : level_(std::move(level)) {}
    [[nodiscard]] float humidityAt(double x, double z) const override {
        return level_->humidityAt(x, z);
    }

private:
    std::shared_ptr<const terrain::PlanetLevel> level_;
};

constexpr double kStep = 128.0; // meters between the points of a transect

struct Bends {
    double near = 0.0; // mean |second difference| where a line of cell centers lies within kStep
    double far = 0.0;  // the same away from them
    [[nodiscard]] double ratio() const { return near / std::max(far, 1e-12); }
};

// Transects along x (alongX) or z at 24 positions across the planet, 40 cells long each.
Bends bendsOf(const terrain::ClimateField& climate, const terrain::PlanetGrid& grid, bool alongX) {
    double near = 0.0;
    double far = 0.0;
    int nearCount = 0;
    int farCount = 0;
    const int points = static_cast<int>(40.0 * grid.cellSize / kStep);
    for (int t = 0; t < 24; ++t) {
        // Across the transects: never on a line of centers, from 60 degrees south to 60 north.
        const double periodAcross = alongX ? grid.periodZ() : grid.periodX();
        const double periodAlong = alongX ? grid.periodX() : grid.periodZ();
        const double across = (0.8 * (t / 23.0) - 0.4) * periodAcross + 0.37 * grid.cellSize;
        const double start = (std::fmod(t * 0.618, 1.0) - 0.5) * 0.9 * periodAlong;
        const auto h = [&](double along) {
            return static_cast<double>(alongX ? climate.humidityAt(along, across, 0.0f)
                                              : climate.humidityAt(across, along, 0.0f));
        };
        for (int p = 1; p < points; ++p) {
            const double along = start + p * kStep;
            const double bend = std::abs(h(along + kStep) - 2.0 * h(along) + h(along - kStep));
            // The nearest line of cell centers along the transect.
            const double origin = -0.5 * periodAlong + 0.5 * grid.cellSize;
            const double offset = std::remainder(along - origin, grid.cellSize);
            if (std::abs(offset) < kStep) {
                near += bend;
                ++nearCount;
            } else if (std::abs(offset) > 2.0 * kStep) {
                far += bend;
                ++farCount;
            }
        }
    }
    return {near / std::max(nearCount, 1), far / std::max(farCount, 1)};
}

} // namespace

int main() {
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    const terrain::Heightfield relief(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      golden::kSmallPlanet);
    const terrain::PlanetParams params = golden::smallPlanetParams(threads);
    auto level = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, params), params);
    auto source = std::make_shared<const LevelHumidity>(level);
    terrain::ClimateParams still;
    still.humidityWarp = 0.0;
    const terrain::ClimateField raw(terrain::kDefaultWorldSeed, still, golden::kSmallPlanet, source);
    const terrain::ClimateField warped(terrain::kDefaultWorldSeed, {}, golden::kSmallPlanet, source);
    for (const bool alongX : {true, false}) {
        const Bends before = bendsOf(raw, level->grid(), alongX);
        const Bends after = bendsOf(warped, level->grid(), alongX);
        std::printf("along %s: bends on the lines of the cell centers / between them: %.1f without "
                    "the warp, %.2f with it\n",
                    alongX ? "x" : "z", before.ratio(), after.ratio());
        TEST_CHECK(before.ratio() > 5.0); // the test sees the folds of the grid
        TEST_CHECK(after.ratio() < 1.5);  // none left on its lines
    }
    // Deterministic, and the humidity of the source within humidityWarp of the point.
    bool within = true;
    const double warp = terrain::ClimateParams{}.humidityWarp;
    for (int n = 0; n < 200; ++n) {
        const double x = -900'000.0 + n * 9'137.0;
        const double z = -300'000.0 + n * 3'011.0;
        const float h = warped.humidityAt(x, z, 0.0f);
        within = within && h == warped.humidityAt(x, z, 0.0f);
        float lowest = 1.0f;
        float highest = 0.0f;
        for (int a = 0; a < 16; ++a) {
            for (int b = 0; b < 16; ++b) {
                const float s =
                    level->humidityAt(x + (a / 7.5 - 1.0) * warp, z + (b / 7.5 - 1.0) * warp);
                lowest = std::min(lowest, s);
                highest = std::max(highest, s);
            }
        }
        within = within && h >= lowest - 0.02f && h <= highest + 0.02f;
    }
    TEST_CHECK(within);
    return test::result();
}
