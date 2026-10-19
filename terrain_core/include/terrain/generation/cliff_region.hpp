#pragma once

#include "terrain/generation/cliffs.hpp"
#include "terrain/generation/eroded_region.hpp"
#include "terrain/meshing/density_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

struct CliffRegionParams {
    ErodedRegionParams eroded;
    CliffParams cliffs;
};

// Cirque carved just below a cascade of the eroded region.
struct Cirque {
    double x = 0.0; // axis, world coordinates
    double z = 0.0;
    float floor = 0.0f; // water height of the cascade's receiver
    int cascade = 0;    // index in eroded().rivers().cascades

    friend bool operator==(const Cirque&, const Cirque&) = default;
};

// Cliffs, plateaus and gorges of an eroded region, as a 3D density volume on top of it.
// The eroded region (relief, rivers, cascades) is unchanged: its river cells keep their
// water height, which is exactly the surface of the volume there, and its cascades stay
// the only cascades. Deterministic: the same seed, region and parameters always give the
// same result. Outside the region, or where the cliff mask is 0, the density is exactly
// y - eroded().heightAt(x, z).
class CliffRegion {
public:
    CliffRegion(std::uint64_t seed, const HeightmapRegion& region,
                const CliffRegionParams& params = {});

    // Cliff state of the column (x, z): eroded height, slope, mask, river proximity
    // (bilinear interpolation of the grids below) and the cirques within reach.
    [[nodiscard]] CliffColumn columnAt(double x, double z) const;
    [[nodiscard]] float densityAt(double x, double y, double z) const;
    // Height of the top of the rock at (x, z).
    [[nodiscard]] float surfaceAt(double x, double z) const;

    [[nodiscard]] const ErodedRegion& eroded() const { return eroded_; }
    [[nodiscard]] const CliffField& field() const { return field_; }
    [[nodiscard]] const std::vector<Cirque>& cirques() const { return cirques_; }

    // Grids indexed like eroded().grid(), at its samples:
    [[nodiscard]] const HeightGrid& surface() const { return surface_; } // surfaceAt
    [[nodiscard]] const std::vector<float>& mask() const { return mask_; } // M
    // Distance to the nearest river cell (capped at kNoRiverDistance), the water height of
    // that cell (its height in eroded().grid()) and the gorge half-width it gives.
    [[nodiscard]] const std::vector<float>& riverDistance() const { return riverDistance_; }
    [[nodiscard]] const std::vector<float>& riverLevel() const { return riverLevel_; }
    [[nodiscard]] const std::vector<float>& gorgeHalfWidth() const { return gorgeHalfWidth_; }

    // Weight of the cliffs near the region border: 0 on the border ring and outside, 1 from
    // params.borderMargin cells inward, smooth in between.
    [[nodiscard]] float borderWeightAt(double x, double z) const;

private:
    ErodedRegion eroded_;
    CliffField field_;
    std::vector<float> riverDistance_;
    std::vector<float> riverLevel_;
    std::vector<float> gorgeHalfWidth_;
    std::vector<Cirque> cirques_;
    std::vector<float> mask_;
    HeightGrid surface_;
};

// Columns of a chunk, sampled every spacing meters: kGridSamples x kGridSamples columns at
// (originX + i * spacing, originZ + k * spacing), with their surface heights.
struct CliffChunkColumns {
    double spacing = kCellSizeMeters;
    std::vector<CliffColumn> columns; // x varies fastest
    float minHeight = 0.0f;           // lowest and highest surface of the columns
    float maxHeight = 0.0f;

    [[nodiscard]] const CliffColumn& at(int x, int z) const {
        return columns[static_cast<std::size_t>(z * kGridSamples + x)];
    }
};

[[nodiscard]] CliffChunkColumns sampleCliffColumns(const CliffRegion& region, double originX,
                                                   double originZ,
                                                   double spacing = kCellSizeMeters);

// Density grid of the chunk whose lowest samples are at originY, with the same spacing
// vertically: sample (i, j, k) holds field.density(columns.at(i, k), originY + j * spacing).
// With spacing = kCellSizeMeters the mesh is in meters; otherwise Marching Cubes gives it
// in units of spacing meters (a scale model).
[[nodiscard]] DensityGrid cliffDensityFromColumns(const CliffField& field,
                                                  const CliffChunkColumns& columns,
                                                  double originY);

} // namespace terrain
