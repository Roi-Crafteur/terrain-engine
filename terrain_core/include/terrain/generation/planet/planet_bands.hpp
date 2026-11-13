#pragma once

// The bands of the prevailing winds of the planet level, joined smoothly (task 11C, step 1).
//
// The winds blow by bands of latitude (PlanetClimateParams): the trades from the equator to 30
// degrees, the westerlies from 30 to 60, the polar easterlies beyond. Marched band by band, the
// rain jumped where the bands meet: the wind turned at once, its moisture came from the other
// side of the continent and the rows stopped feeding each other there, so that the rain and all
// that reads it (the humidity of the biomes, the deserts, the maps of the settlement) showed
// straight lines every 30 degrees.
//
// Here the six bands (from the south pole to the north one) meet along five lines, at 60 and 30
// degrees south, the equator, 30 and 60 degrees north, each moved north and south by a slow noise
// of the longitude (bandShift degrees at most, over bandShiftWavelength meters): never a straight
// line. Each band is marched with its own wind over its rows and bandJoin degrees beyond its
// lines; a cell within bandJoin degrees of a line takes the rain of the two bands, weighted by a
// smoothstep of its distance to the line. Away from the lines (more than bandJoin + bandShift
// degrees), every cell keeps its band alone: the great zones of the climate stay where they are.
//
// Deterministic: the lines are pure functions of the seed and of the column, periodic on the
// world (an integer number of waves along x).
//
// Without smoothBands (the default until the flag smooth_bands is on), the joins and the shifts
// are 0: each row belongs to one band, marched as before, and the climate is the one of before,
// bit for bit.

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/planet/planet_wind.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain::planet {

inline constexpr int kWindBands = 6;          // south polar to north polar
inline constexpr int kBandLines = kWindBands - 1;

class WindBands {
public:
    // The bands of the grid; seed draws the waves of their lines.
    WindBands(const PlanetGrid& grid, const PlanetClimateParams& params, std::uint64_t seed);

    // Latitude of a row, degrees, positive to the north (the north is z < 0), in (-90, 90).
    [[nodiscard]] double latitude(int row) const;
    // The line between band k and band k + 1 at a column, degrees of latitude: its nominal
    // latitude (-60, -30, 0, 30, 60) moved by its wave.
    [[nodiscard]] double line(int k, int column) const;
    // The unit wind of a band (the trades, westerlies or easterlies of its hemisphere).
    [[nodiscard]] PlanetWind wind(int band) const;
    // How much of band `band` the cell (column, row) takes, in [0, 1]; the weights of a cell sum
    // to 1 and at most two bands weigh on it.
    [[nodiscard]] float weight(int band, int column, int row) const;
    // The rows band `band` is marched over, in the order of its wind (the first takes no moisture
    // from another row): those where it weighs on at least one cell.
    [[nodiscard]] std::vector<int> chain(int band) const;
    // How many bands weigh on at least one cell of a row (1 or 2).
    [[nodiscard]] int bandsOf(int row) const;

    // The nominal latitude of line k, degrees.
    [[nodiscard]] static double nominal(int k) { return 30.0 * (k - 2); }

private:
    [[nodiscard]] bool reaches(int band, int row) const;

    PlanetGrid grid_;
    PlanetClimateParams params_;
    double join_ = 0.0;  // bandJoin with smoothBands, else 0
    double shift_ = 0.0; // bandShift with smoothBands, else 0
    // lines_[k][column]: the latitude of line k at the column.
    std::array<std::vector<double>, kBandLines> lines_;
    std::array<double, kBandLines> lowest_{};
    std::array<double, kBandLines> highest_{};
};

} // namespace terrain::planet
