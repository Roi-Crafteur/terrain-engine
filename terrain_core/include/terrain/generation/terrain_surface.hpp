#pragma once

#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/sculpted_region.hpp"
#include "terrain/generation/water_kind.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace terrain {

// What the climate, the biomes, the surface materials and the vegetation read from the relief
// at one sample. It is a copy: nothing is ever written back to the relief.
struct SurfaceSite {
    float ground = 0.0f; // top of the solid ground (rock or soil), meters
    // Top of the column: kSeaLevel over the ocean, the ground elsewhere (the water of a river
    // cell is at the ground height).
    float waterLevel = 0.0f;
    WaterKind water = WaterKind::None;
    // Gradient of the ground (rise over run), by central differences over one sample.
    float gradX = 0.0f;
    float gradZ = 0.0f;
    // Largest height difference to a 4-neighbor sample divided by the spacing: the steepest
    // local slope, which the gradient averages out next to a wall.
    float steepness = 0.0f;
    float riverDistance = kNoRiverDistance; // meters to the nearest river sample, capped
    float continentalness = 0.0f;           // of the Heightfield, in [-1, 1]
    bool rockAboveAir = false;              // rock over air: overhang or arch
    // At a vertical wall of the cliffs: the surface drops by at least half a stratum to a
    // 4-neighbor sample, where the cliff mask is not 0.
    bool cliffWall = false;

    [[nodiscard]] bool dry() const { return water == WaterKind::None; }
    [[nodiscard]] float waterDepth() const { return waterLevel - ground; }
};

// Surface of a square region of the world, one site per sample. Sample (column, row) lies
// at the pixel center of HeightGrid: center + (i + 0.5 - size / 2) * metersPerPixel.
struct TerrainSurface {
    HeightmapRegion region;
    std::vector<SurfaceSite> sites; // row by row, columns vary fastest

    [[nodiscard]] int size() const { return region.size; }
    [[nodiscard]] double sampleX(int column) const;
    [[nodiscard]] double sampleZ(int row) const;
    [[nodiscard]] std::size_t index(int column, int row) const {
        return static_cast<std::size_t>(row * region.size + column);
    }
    [[nodiscard]] const SurfaceSite& at(int column, int row) const {
        return sites[index(column, row)];
    }
};

// Grids of a relief, one value per sample of the region, row by row. Empty masks mean none.
struct SurfaceGrids {
    std::span<const float> ground = {};              // top of the solid ground
    std::span<const std::uint8_t> river = {};        // 1 on river samples
    std::span<const std::uint8_t> rockAboveAir = {}; // 1 where rock lies above air
    std::span<const std::uint8_t> cliffWall = {};    // 1 at the vertical walls of the cliffs
};

// Surface of grids of a relief: the ground is copied as is, the ocean is the ground below
// kSeaLevel outside the river samples, the gradient and the steepness come from the
// neighbor samples (one-sided on the border) and the river distance from a chamfer distance
// transform. The continentalness is read from field at each sample.
[[nodiscard]] TerrainSurface surfaceFromGrids(const Heightfield& field,
                                              const HeightmapRegion& region,
                                              const SurfaceGrids& grids);

// Surface of the Heightfield alone (no river, no cliff). Every value of a sample is computed
// from the field at its position and at fixed offsets of one pixel, never from the other
// samples: two regions with the same metersPerPixel agree on the samples they share, so
// regions and chunks join without a seam. ground is exactly Heightfield::heightAt, and the
// water that of HeightSample (the sea where the geography puts it, not below kSeaLevel).
[[nodiscard]] TerrainSurface surfaceFromHeightfield(const Heightfield& field,
                                                    const HeightmapRegion& region);

// Surface of an eroded region: its final grid and its river cells.
[[nodiscard]] TerrainSurface surfaceFromEroded(const ErodedRegion& eroded);

// Surface of a sculpted region: the top of its rock (SculptedRegion::surface), the river
// cells of its eroded region, the samples where rock lies above air and the vertical walls
// of its cliffs.
[[nodiscard]] TerrainSurface surfaceFromSculpted(const SculptedRegion& sculpted);

} // namespace terrain
