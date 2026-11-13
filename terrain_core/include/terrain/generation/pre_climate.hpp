#pragma once

// The climate of the planetary geography before the relief, a module shared by the provinces of
// the relief that the climate places (agent 46: the plateaus and the karst; agent 52: the hollows
// of the plains). The relief cannot read the climate of the planet level, which is built from it:
// these provinces read this one instead, built from the geography alone. It is the continental
// climate of the planet level (planet::computeClimate: the prevailing winds, the orographic rain
// and its shadows, the eddies of the nearby seas, the blur) run on a coarse grid over the regional
// elevation of the geography (its land, plus a share of the mountains standing for the floors the
// planet level reads), the sea where the geography puts it. At the scale of the provinces it
// follows the climate of the planet level (pre_climate_test measures how well, and where it errs
// most). Deterministic, the same bits on every platform whatever the number of threads; built
// once per geography and shared (PreClimate::shared).

#include "terrain/generation/geography.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/world_size.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain {

// The largest side of the cells of the grid of the pre-climate: that of the nodes of the map of
// the geography (GeographyParams::cellSize). A smaller world takes cells of a 128th of its side
// along z at most (a power of two, so that they divide both sides).
inline constexpr double kPreClimateCell = 65'536.0;

class PreClimate {
public:
    // The climate of geography with the parameters of the climate of the planet level. The
    // construction runs on up to threads threads (0: the hardware concurrency).
    explicit PreClimate(const Geography& geography, const PlanetClimateParams& params = {},
                        int threads = 0);

    // The pre-climate of the geography of these arguments (Geography::shared), built once and
    // shared while held: the same object for the same arguments. Thread-safe.
    [[nodiscard]] static std::shared_ptr<const PreClimate>
    shared(std::uint64_t geographySeed, const GeographyParams& params, const WorldSize& world);

    // The side of the cells for a world (kPreClimateCell at most).
    [[nodiscard]] static double cellSizeFor(const WorldSize& world);

    [[nodiscard]] const PlanetGrid& grid() const { return grid_; }
    [[nodiscard]] const PlanetClimateParams& params() const { return params_; }

    // A cell: the elevation the air flows over (meters; the floor of the sea at sea), whether it
    // is ocean, its precipitation (mm/yr), its mean temperature at its elevation (Celsius) and its
    // humidity P / (P + PET), PET read as 300 mm/yr at least, as PlanetLevel::humidityAt.
    [[nodiscard]] float height(std::size_t cell) const { return height_[cell]; }
    [[nodiscard]] bool ocean(std::size_t cell) const { return ocean_[cell] != 0; }
    [[nodiscard]] float precipitation(std::size_t cell) const { return precipitation_[cell]; }
    [[nodiscard]] float temperature(std::size_t cell) const { return temperature_[cell]; }
    [[nodiscard]] float humidity(std::size_t cell) const { return humidity_[cell]; }

    // Bilinear on the torus at (x, z), as PlanetLevel::sampleAt and humidityAt.
    [[nodiscard]] float humidityAt(double x, double z) const;
    [[nodiscard]] float temperatureAt(double x, double z) const;
    [[nodiscard]] float precipitationAt(double x, double z) const;

    // 64-bit FNV-1a of the raw bits of every cell.
    [[nodiscard]] std::uint64_t checksum() const;
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    PlanetClimateParams params_;
    PlanetGrid grid_;
    std::vector<float> height_;
    std::vector<std::uint8_t> ocean_;
    std::vector<float> precipitation_;
    std::vector<float> temperature_;
    std::vector<float> humidity_;
};

} // namespace terrain
