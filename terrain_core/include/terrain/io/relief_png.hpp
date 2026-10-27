#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/io/png.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace terrain {

// Control maps of the relief at every scale, from the whole planet to a walk: hypsometric
// colors up to the highest summits, shaded by the slope lit from the north-west, slopes
// exaggerated more on coarser maps (the planet is flat at 8 km per pixel).

// Heights of columns x rows samples spacing meters apart, sample (i, j) at
// (originX + (i + 0.5) spacing, originZ + (j + 0.5) spacing): +X to the right, +Z down.
// wraps: the grid covers exactly the whole world, so that its first and last columns (rows)
// are neighbors, as on the torus.
struct ReliefGrid {
    int columns = 0;
    int rows = 0;
    double spacing = 1.0;
    double originX = 0.0;
    double originZ = 0.0;
    bool wraps = false;
    std::vector<float> heights; // row by row
    // The water of the samples (HeightSample::water and waterLevel), row by row; empty: the sea
    // below kSeaLevel.
    std::vector<WaterKind> water;
    std::vector<float> waterLevels;

    [[nodiscard]] std::size_t index(int i, int j) const {
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(columns) +
               static_cast<std::size_t>(i);
    }
    [[nodiscard]] float at(int i, int j) const { return heights[index(i, j)]; }
    [[nodiscard]] bool sea(int i, int j) const {
        return water.empty() ? at(i, j) < kSeaLevel : water[index(i, j)] == WaterKind::Ocean;
    }
    [[nodiscard]] double x(int i) const { return originX + (i + 0.5) * spacing; }
    [[nodiscard]] double z(int j) const { return originZ + (j + 0.5) * spacing; }
};

// A window of columns x rows samples centered on (centerX, centerZ), sampled by samplesAt with
// up to threads threads (0: the hardware concurrency) in fixed blocks of rows: the same bits
// whatever the number of threads. Throws std::invalid_argument for an empty window or a
// spacing that is not finite and above 0.
[[nodiscard]] ReliefGrid sampleReliefWindow(const Heightfield& field, double centerX,
                                            double centerZ, double spacing, int columns,
                                            int rows, int threads);

// The whole world on cells of cellSize meters (both sides of the world must be multiples of
// it), from the corner (-world.x / 2, -world.z / 2): a wrapping grid. Throws
// std::invalid_argument otherwise.
[[nodiscard]] ReliefGrid sampleReliefWorld(const Heightfield& field, double cellSize,
                                           int threads);

// The grid averaged over blocks of factor x factor samples (the last partial ones dropped),
// still wrapping if the grid wraps and factor divides both sides; the sea where most of a block
// is, its level the mean of theirs.
[[nodiscard]] ReliefGrid downsampleRelief(const ReliefGrid& grid, int factor);

// Slope exaggeration of the shading for a map of this spacing: 1 at 10 m, growing as the
// square root of the spacing, at most 30.
[[nodiscard]] double reliefExaggeration(double spacing);

// The hypsometric color of a height before the shading, red, green and blue in [0, 255]: the
// tints of the sea below kSeaLevel, of the land above.
[[nodiscard]] std::array<float, 3> reliefColor(float height);
// The color of a sample: the sea and the land as reliefColor, the dry land below the sea level (a
// depression) in tints of salt and clay.
[[nodiscard]] std::array<float, 3> reliefColor(float height, bool sea);

// The hypsometric shaded map of the grid, one pixel per sample (RGB).
[[nodiscard]] Image renderRelief(const ReliefGrid& grid);

} // namespace terrain
