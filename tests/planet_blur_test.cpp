// The blurs of the climate in place and in flux (planet_blur, phase 11D): the same bits as the
// passes on whole grids they replace, for windows small and large, up to the whole grid, on any
// number of threads.
#include "planet_blur.hpp"

#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::PlanetGrid;
namespace planet = terrain::planet;

std::vector<float> noiseGrid(const PlanetGrid& grid, std::uint64_t seed) {
    std::vector<float> values(grid.cells());
    for (std::size_t c = 0; c < values.size(); ++c) {
        const std::uint64_t bits = terrain::noise::deriveSeed(seed, c);
        values[c] = static_cast<float>(bits >> 40) / 16777216.0f * 3000.0f - 500.0f;
    }
    return values;
}

// The passes on whole grids of phase 11B, as they were.
void denseBoxPass(const PlanetGrid& grid, std::vector<float>& values, int radius) {
    std::vector<float> along(values.size());
    const double width = 2.0 * radius + 1.0;
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            double sum = 0.0;
            for (int d = -radius; d <= radius; ++d) {
                sum += values[grid.index(grid.wrapColumn(i + d), j)];
            }
            along[grid.index(i, j)] = static_cast<float>(sum / width);
        }
    }
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            double sum = 0.0;
            for (int d = -radius; d <= radius; ++d) {
                sum += along[grid.index(i, grid.wrapRow(j + d))];
            }
            values[grid.index(i, j)] = static_cast<float>(sum / width);
        }
    }
}

void denseBlurPass(const PlanetGrid& grid, std::vector<float>& values) {
    std::vector<float> along(values.size());
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            const double sum = static_cast<double>(values[grid.index(grid.wrapColumn(i - 1), j)]) +
                               2.0 * values[grid.index(i, j)] +
                               values[grid.index(grid.wrapColumn(i + 1), j)];
            along[grid.index(i, j)] = static_cast<float>(0.25 * sum);
        }
    }
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            const double sum = static_cast<double>(along[grid.index(i, grid.wrapRow(j - 1))]) +
                               2.0 * along[grid.index(i, j)] +
                               along[grid.index(i, grid.wrapRow(j + 1))];
            values[grid.index(i, j)] = static_cast<float>(0.25 * sum);
        }
    }
}

void denseRunningPass(const PlanetGrid& grid, std::vector<double>& values, int radius) {
    std::vector<double> along(values.size());
    const double width = 2.0 * radius + 1.0;
    for (int j = 0; j < grid.nz; ++j) {
        double sum = 0.0;
        for (int d = -radius; d <= radius; ++d) {
            sum += values[grid.index(grid.wrapColumn(d), j)];
        }
        for (int i = 0; i < grid.nx; ++i) {
            along[grid.index(i, j)] = std::max(sum, 0.0) / width;
            sum += values[grid.index(grid.wrapColumn(i + radius + 1), j)] -
                   values[grid.index(grid.wrapColumn(i - radius), j)];
        }
    }
    for (int i = 0; i < grid.nx; ++i) {
        double sum = 0.0;
        for (int d = -radius; d <= radius; ++d) {
            sum += along[grid.index(i, grid.wrapRow(d))];
        }
        for (int j = 0; j < grid.nz; ++j) {
            values[grid.index(i, j)] = std::max(sum, 0.0) / width;
            sum += along[grid.index(i, grid.wrapRow(j + radius + 1))] -
                   along[grid.index(i, grid.wrapRow(j - radius))];
        }
    }
}

template <class T> bool sameBits(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (std::bit_cast<std::uint64_t>(static_cast<double>(a[k])) !=
            std::bit_cast<std::uint64_t>(static_cast<double>(b[k]))) {
            return false;
        }
    }
    return true;
}

void testBoxAndBlur(int nx, int nz, int radius) {
    const PlanetGrid grid{nx, nz, 1000.0};
    const std::vector<float> start = noiseGrid(grid, static_cast<std::uint64_t>(nx * 1000 + nz));
    std::vector<float> dense = start;
    denseBoxPass(grid, dense, radius);
    denseBlurPass(grid, dense);
    for (const int threads : {1, 3}) {
        std::vector<float> values = start;
        planet::boxPassInPlace(grid, values, radius, threads);
        planet::blurPassInPlace(grid, values, threads);
        const bool same = sameBits(values, dense);
        if (!same) {
            std::printf("box and blur differ: %d x %d, radius %d, %d threads\n", nx, nz, radius,
                        threads);
        }
        TEST_CHECK(same);
    }
}

void testStorm(int nx, int nz, int radius) {
    const PlanetGrid grid{nx, nz, 1000.0};
    std::vector<std::uint8_t> ocean(grid.cells());
    std::vector<double> rowMoisture(static_cast<std::size_t>(nz));
    for (std::size_t c = 0; c < ocean.size(); ++c) {
        ocean[c] = (terrain::noise::deriveSeed(7, c) & 3U) != 0 ? planet::kOcean : planet::kLand;
    }
    for (int j = 0; j < nz; ++j) {
        rowMoisture[static_cast<std::size_t>(j)] =
            1.0e9 * (1.0 + 0.37 * static_cast<double>(terrain::noise::deriveSeed(9, j) >> 44));
    }
    std::vector<double> dense(grid.cells(), 0.0);
    for (std::size_t c = 0; c < dense.size(); ++c) {
        if (ocean[c] != planet::kLand) {
            dense[c] = rowMoisture[static_cast<std::size_t>(grid.row(c))];
        }
    }
    denseRunningPass(grid, dense, radius);
    denseRunningPass(grid, dense, radius);
    std::vector<double> rows(grid.cells(), -1.0);
    int next = 0;
    bool ordered = true;
    planet::stormMoistureRows(
        grid, ocean, rowMoisture, radius, [&](int j, const std::vector<double>& values) {
            ordered = ordered && j == next++;
            std::copy(values.begin(), values.end(),
                      rows.begin() + static_cast<std::ptrdiff_t>(grid.index(0, j)));
        });
    const bool same = ordered && next == nz && sameBits(rows, dense);
    if (!same) {
        std::printf("storm moisture differs: %d x %d, radius %d\n", nx, nz, radius);
    }
    TEST_CHECK(same);
}

} // namespace

int main() {
    // Many batches of rows; a window as tall as the grid (2 radius + 1 = nz); taller (the whole
    // grid at once); a wide window along x.
    testBoxAndBlur(23, 150, 6);
    testBoxAndBlur(31, 33, 16);
    testBoxAndBlur(12, 9, 6);
    testBoxAndBlur(9, 40, 11);
    testBoxAndBlur(256, 128, 6);
    // Radii up to (nz - 1) / 2, on even and odd heights; one row per batch of the sweeps.
    testStorm(20, 150, 1);
    testStorm(80, 150, 30);
    testStorm(15, 33, 7);
    testStorm(40, 33, 16);
    testStorm(31, 32, 15);
    testStorm(256, 128, 63);
    return test::result();
}
