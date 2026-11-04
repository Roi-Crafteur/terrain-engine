#include "terrain/generation/planet/planet_bands.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace terrain::planet {

namespace {

constexpr std::uint64_t kLineStream = 0x7B1D5EAD;

// A value in [-1, 1) drawn from a seed and a stream.
double bandDrawn(std::uint64_t seed, std::uint64_t stream) {
    const std::uint64_t bits = noise::deriveSeed(seed, stream) >> 11;
    return 2.0 * static_cast<double>(bits) * 0x1.0p-53 - 1.0;
}

// A periodic value noise along x: count values around the world, joined by a quintic.
double bandWave(std::uint64_t seed, int count, double turns) {
    const double at = turns * static_cast<double>(count);
    const double cell = std::floor(at);
    const double t = at - cell;
    const auto c = static_cast<std::int64_t>(cell);
    const auto a = static_cast<std::uint64_t>(((c % count) + count) % count);
    const auto b = static_cast<std::uint64_t>((((c + 1) % count) + count) % count);
    const double fade = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    const double va = bandDrawn(seed, a);
    return va + (bandDrawn(seed, b) - va) * fade;
}

double bandHermite(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

bool northernBand(int band) {
    return band >= kWindBands / 2;
}

} // namespace

WindBands::WindBands(const PlanetGrid& grid, const PlanetClimateParams& params, std::uint64_t seed)
    : grid_(grid), params_(params) {
    if (grid.nx <= 0 || grid.nz <= 0) {
        throw std::invalid_argument("WindBands: an empty grid");
    }
    if (params.bandJoin < 0.0 || params.bandShift < 0.0 ||
        params.bandJoin + params.bandShift >= 15.0) {
        throw std::invalid_argument("WindBands: the joins of the bands would overlap");
    }
    const double waves = std::max(1.0, std::round(grid.periodX() / params.bandShiftWavelength));
    const int coarse = static_cast<int>(waves);
    const int fine = 2 * coarse + 1;
    for (int k = 0; k < kBandLines; ++k) {
        const std::uint64_t lineSeed =
            noise::deriveSeed(seed, kLineStream + static_cast<std::uint64_t>(k));
        std::vector<double>& line = lines_[static_cast<std::size_t>(k)];
        line.resize(static_cast<std::size_t>(grid.nx));
        for (int i = 0; i < grid.nx; ++i) {
            const double turns = (static_cast<double>(i) + 0.5) / static_cast<double>(grid.nx);
            const double wave = (2.0 * bandWave(lineSeed, coarse, turns) +
                                 bandWave(noise::deriveSeed(lineSeed, 1), fine, turns)) /
                                3.0;
            line[static_cast<std::size_t>(i)] = nominal(k) + params.bandShift * wave;
        }
        lowest_[static_cast<std::size_t>(k)] = *std::min_element(line.begin(), line.end());
        highest_[static_cast<std::size_t>(k)] = *std::max_element(line.begin(), line.end());
    }
}

double WindBands::latitude(int row) const {
    return -180.0 * grid_.centerZ(row) / grid_.periodZ();
}

double WindBands::line(int k, int column) const {
    return lines_[static_cast<std::size_t>(k)][static_cast<std::size_t>(column)];
}

PlanetWind WindBands::wind(int band) const {
    // The trades blow to the west, the westerlies to the east, the polar easterlies to the west;
    // their meridional components are poleward when positive.
    const int fromEquator =
        northernBand(band) ? band - kWindBands / 2 : kWindBands / 2 - 1 - band;
    double zonal = -1.0;
    double meridional = params_.tradeMeridional;
    if (fromEquator == 1) {
        zonal = 1.0;
        meridional = params_.westerlyMeridional;
    } else if (fromEquator == 2) {
        meridional = params_.polarMeridional;
    }
    // Poleward is -z in the north, +z in the south.
    const double poleward = northernBand(band) ? -1.0 : 1.0;
    const double length = std::sqrt(1.0 + meridional * meridional);
    return {zonal / length, poleward * meridional / length};
}

float WindBands::weight(int band, int column, int row) const {
    const double at = latitude(row);
    const double join = params_.bandJoin;
    // How far the cell lies past the line below the band, and past the line above it.
    const double above = band == 0 ? 1.0 : bandHermite(-join, join, at - line(band - 1, column));
    const double beyond =
        band == kWindBands - 1 ? 0.0 : bandHermite(-join, join, at - line(band, column));
    return static_cast<float>(above * (1.0 - beyond));
}

bool WindBands::reaches(int band, int row) const {
    const double at = latitude(row);
    const double join = params_.bandJoin;
    const bool overLow = band == 0 || at > lowest_[static_cast<std::size_t>(band - 1)] - join;
    const bool underHigh =
        band == kWindBands - 1 || at < highest_[static_cast<std::size_t>(band)] + join;
    return overLow && underHigh;
}

std::vector<int> WindBands::chain(int band) const {
    std::vector<int> rows;
    for (int j = 0; j < grid_.nz; ++j) {
        if (reaches(band, j)) {
            rows.push_back(j);
        }
    }
    // The rows go north to south (j up); the wind takes the moisture of the row it comes from.
    if (wind(band).z < 0.0) {
        std::reverse(rows.begin(), rows.end());
    }
    return rows;
}

int WindBands::bandsOf(int row) const {
    int count = 0;
    for (int band = 0; band < kWindBands; ++band) {
        count += reaches(band, row) ? 1 : 0;
    }
    return count;
}

} // namespace terrain::planet
