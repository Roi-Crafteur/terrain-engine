#pragma once

#include "terrain/generation/erosion.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/rivers.hpp"

#include <cstdint>

namespace terrain {

struct ErodedRegionParams {
    HeightfieldParams heightfield;
    ErosionParams erosion;
    RiverParams rivers;
};

// A square region of the world after erosion and river generation, built in a fixed order:
// heightfield samples, hydraulic erosion, thermal smoothing, border fade, then the rivers
// (depression filling, drainage, bed carving, cascades) on the eroded relief.
// Deterministic: the same seed, region and parameters always give the same result.
// The Heightfield itself is unchanged: this is a separate pipeline on top of it.
//
// The region is a window of the wrapped world (HeightmapRegion) and may cross its edges: its
// samples read the periodic heightfield, so the same window at another image of its center
// gives the same grids and rivers (bit for bit when the coordinates of the samples are exact).
// Its own objects (the erosion, the rivers, the cascades) live once, in the frame of the window:
// a query (x, z) is first moved to its image nearest to the center (imageX, imageZ), so every
// image of a point gets the same answer, in canonical coordinates as in the frame of the window.
class ErodedRegion {
public:
    // Throws std::invalid_argument for a window wider than the world (validatedRegion) or an
    // invalid world.
    ErodedRegion(std::uint64_t seed, const HeightmapRegion& region,
                 const ErodedRegionParams& params = {}, const WorldSize& world = {});

    // Continuous height of the eroded world: Heightfield::heightAt plus the bilinear
    // interpolation of the change made by the pipeline, which is zero on the border ring.
    // Outside the region it is exactly Heightfield::heightAt.
    [[nodiscard]] float heightAt(double x, double z) const;

    // The image of a coordinate nearest to the center of the region along x or z: the
    // coordinate in the frame of the window (unchanged within half a world of the center).
    [[nodiscard]] double imageX(double x) const;
    [[nodiscard]] double imageZ(double z) const;

    // World coordinates of the sample (column, row) of the grids.
    [[nodiscard]] double sampleX(int column) const;
    [[nodiscard]] double sampleZ(int row) const;

    [[nodiscard]] const Heightfield& field() const { return field_; }
    [[nodiscard]] const HeightmapRegion& region() const { return region_; }
    [[nodiscard]] const HeightGrid& rawGrid() const { return raw_; }    // before erosion
    [[nodiscard]] const HeightGrid& grid() const { return grid_; }      // final heights
    [[nodiscard]] const RiverNetwork& rivers() const { return rivers_; }

private:
    Heightfield field_;
    HeightmapRegion region_;
    HeightGrid raw_;
    HeightGrid grid_;
    RiverNetwork rivers_;
};

} // namespace terrain
