#pragma once

#include "terrain/chunk_coord.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <vector>

namespace terrain {

// Cells along each axis of a chunk, and samples (cell corners) along each axis.
inline constexpr int kChunkCells = 16;
inline constexpr int kGridSamples = kChunkCells + 1;

// Distance between two neighboring samples, in meters.
inline constexpr double kCellSizeMeters = kChunkSizeMeters / kChunkCells;

// Densities sampled at the corners of the 16 x 16 x 16 cells of a chunk.
// Signed distance convention: negative is solid, zero or positive is air.
class DensityGrid {
public:
    explicit DensityGrid(float fill) : samples_(kSampleCount, fill) {}

    [[nodiscard]] float at(int x, int y, int z) const { return samples_[index(x, y, z)]; }
    void set(int x, int y, int z, float density) { samples_[index(x, y, z)] = density; }

private:
    static constexpr auto kSamplesPerAxis = static_cast<std::size_t>(kGridSamples);
    static constexpr std::size_t kSampleCount = kSamplesPerAxis * kSamplesPerAxis * kSamplesPerAxis;

    static std::size_t index(int x, int y, int z) {
        assert(x >= 0 && x < kGridSamples && y >= 0 && y < kGridSamples && z >= 0 &&
               z < kGridSamples);
        return static_cast<std::size_t>((z * kGridSamples + y) * kGridSamples + x);
    }

    std::vector<float> samples_; // x varies fastest
};

template <class F>
concept DensityFunction = std::invocable<const F&, double, double, double>;

// Samples density(x, y, z), in world meters, at origin + (i, j, k) * kCellSizeMeters.
template <DensityFunction F>
[[nodiscard]] DensityGrid sampleDensity(const F& density, double originX, double originY,
                                        double originZ) {
    DensityGrid grid(0.0f);
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            for (int x = 0; x < kGridSamples; ++x) {
                const double value = density(originX + x * kCellSizeMeters,
                                             originY + y * kCellSizeMeters,
                                             originZ + z * kCellSizeMeters);
                grid.set(x, y, z, static_cast<float>(value));
            }
        }
    }
    return grid;
}

} // namespace terrain
