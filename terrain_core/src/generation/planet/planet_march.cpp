#include "planet_march.hpp"

#include "planet_parallel.hpp"
#include "terrain/generation/planet/planet_moisture.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace terrain::planet {

namespace {

// The rows two bands share: the weighted rain of the band marched first, until the other one.
struct BandSharedRows {
    explicit BandSharedRows(int rows)
        : locks(static_cast<std::size_t>(rows)), partial(static_cast<std::size_t>(rows)) {}
    std::vector<std::mutex> locks;
    std::vector<std::vector<float>> partial;
};

struct BandMarchInputs {
    const PlanetGrid& grid;
    std::vector<float>& values;
    const LandIndex& land;
    const std::vector<float>& rowTemperature;
    const PlanetClimateParams& params;
    const WindBands& bands;
    BandSharedRows& shared;
};

// The rain of row j of band `band` into the grid: in place where the band weighs alone on it,
// else added to that of the other band once both are known.
void storeRowRain(const BandMarchInputs& in, int band, int j, const std::vector<float>& rain,
                  std::vector<float>& weighted) {
    const std::size_t first = in.grid.index(0, j);
    if (in.bands.bandsOf(j) == 1) {
        std::copy(rain.begin(), rain.end(), in.values.begin() + static_cast<std::ptrdiff_t>(first));
        return;
    }
    for (int i = 0; i < in.grid.nx; ++i) {
        weighted[static_cast<std::size_t>(i)] =
            in.bands.weight(band, i, j) * rain[static_cast<std::size_t>(i)];
    }
    const std::lock_guard<std::mutex> lock(in.shared.locks[static_cast<std::size_t>(j)]);
    std::vector<float>& partial = in.shared.partial[static_cast<std::size_t>(j)];
    if (partial.empty()) {
        partial = weighted;
        return;
    }
    for (std::size_t i = 0; i < partial.size(); ++i) {
        in.values[first + i] = partial[i] + weighted[i];
    }
    freeVector(partial);
}

void marchOneBand(const BandMarchInputs& in, int band) {
    const PlanetGrid& grid = in.grid;
    MoistureOutput previous;
    MoistureOutput out;
    std::vector<std::uint8_t> ocean(static_cast<std::size_t>(grid.nx)); // of the row
    std::vector<float> weighted(static_cast<std::size_t>(grid.nx));
    bool fed = false;
    for (const int j : in.bands.chain(band)) {
        const std::size_t first = grid.index(0, j);
        for (std::size_t i = 0; i < ocean.size(); ++i) {
            ocean[i] = in.land.land(first + i) ? kLand : kOcean;
        }
        MoistureRow row;
        row.height = in.values.data() + first;
        row.ocean = ocean.data();
        row.upwindMoisture = fed ? previous.moisture.data() : nullptr;
        row.upwindAir = fed ? previous.air.data() : nullptr;
        row.nx = grid.nx;
        row.cellSize = grid.cellSize;
        row.temperature = in.rowTemperature[static_cast<std::size_t>(j)];
        row.latitude = rowLatitude(grid, j, in.params.sphereLatitude);
        row.wind = in.bands.wind(band);
        marchMoistureRow(row, in.params, out);
        storeRowRain(in, band, j, out.precipitation, weighted);
        std::swap(previous, out);
        fed = true;
    }
}

} // namespace

void marchBands(const PlanetGrid& grid, std::vector<float>& values, const LandIndex& land,
                const std::vector<float>& rowTemperature, const PlanetClimateParams& params,
                const WindBands& bands, int threads, std::vector<float>& landSurface) {
    landSurface.assign(land.landCells(), 0.0f);
    land.forEachLand([&](std::size_t cell, std::size_t slot) { landSurface[slot] = values[cell]; });
    BandSharedRows shared(grid.nz);
    const BandMarchInputs in{grid, values, land, rowTemperature, params, bands, shared};
    parallelBlocks(kWindBands, 1, threads, [&in](int begin, int end) {
        for (int band = begin; band < end; ++band) {
            marchOneBand(in, band);
        }
    });
}

} // namespace terrain::planet
