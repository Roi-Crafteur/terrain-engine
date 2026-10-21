#pragma once

#include "terrain/generation/arches.hpp"
#include "terrain/generation/cliff_region.hpp"
#include "terrain/generation/overhangs.hpp"
#include "terrain/meshing/density_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

struct SculptedRegionParams {
    CliffRegionParams cliffs;
    OverhangParams overhangs;
    ArchParams arches;
    // The overhangs fade out from archFade meters beyond the reach of an arch to its reach.
    double archFade = 20.0;
};

inline constexpr int kNoArch = -1;

// Everything the density of a column (x, z) depends on, apart from the height y.
struct SculptedColumn {
    double x = 0.0;
    double z = 0.0;
    CliffColumn cliff;     // with its overhangs
    float overhang = 0.0f; // weight of the overhangs, in [0, 1]
    int arch = kNoArch;    // index in SculptedRegion::arches() of the arch within reach
};

// Overhangs and arches carved into the cliffs of a region (Phase 5A), as a 3D density volume:
//   D = arch(cliff density with the overhangs of the column)
// where the arch, if one is within reach, carves its opening and adds its rock. The cliff
// region is unchanged, and so are its rivers, cascades, cirques, coasts and oceans: neither
// overhangs (0 within half the river reach, below the cliff mask threshold) nor arches
// (placed away from all water) reach them. Where the overhang weight is 0 and no arch is
// within reach, the density is exactly CliffRegion::densityAt. Deterministic. Every query is
// answered at the image of (x, z) nearest to the center of the region, in whose frame the arches
// lie (ErodedRegion): SculptedColumn::x and z are in that frame.
class SculptedRegion {
public:
    SculptedRegion(std::uint64_t seed, const HeightmapRegion& region,
                   const SculptedRegionParams& params = {}, const WorldSize& world = {});

    [[nodiscard]] SculptedColumn columnAt(double x, double z) const;
    [[nodiscard]] float density(const SculptedColumn& column, double y) const;
    [[nodiscard]] float densityAt(double x, double y, double z) const;

    // Top of the rock of a column, and whether it holds air below rock (under an overhang or
    // an arch). Columns without overhang or arch have a single surface, found like
    // CliffField::topSurface; the others are searched from the top of their bounds down with
    // steps of at most a meter, then by bisection.
    struct Scan {
        float top = 0.0f;
        bool airBelowRock = false;
    };
    [[nodiscard]] Scan scan(const SculptedColumn& column) const;
    [[nodiscard]] float surfaceAt(double x, double z) const;

    // Bounds of the rock surfaces of a column: every surface of it lies strictly between,
    // with air at high and rock at low.
    struct Bounds {
        float low = 0.0f;
        float high = 0.0f;
    };
    [[nodiscard]] Bounds bounds(const SculptedColumn& column) const;

    // Overhang control mask at (x, z) (OverhangField::controlAt, faded near the arches).
    [[nodiscard]] float controlAt(double x, double z, const CliffColumn& column) const;

    [[nodiscard]] const CliffRegion& cliffs() const { return cliffs_; }
    [[nodiscard]] const OverhangField& overhangs() const { return overhangs_; }
    [[nodiscard]] const ArchField& archField() const { return archField_; }
    [[nodiscard]] const std::vector<Arch>& arches() const { return arches_; }

    // Grids indexed like cliffs().eroded().grid(), at its samples: overhang control mask,
    // overhang weight, top of the rock and 1 where rock lies above air.
    [[nodiscard]] const std::vector<float>& controlMask() const { return control_; }
    [[nodiscard]] const std::vector<float>& overhangWeight() const { return weight_; }
    [[nodiscard]] const HeightGrid& surface() const { return surface_; }
    [[nodiscard]] const std::vector<std::uint8_t>& airBelowRock() const { return airBelowRock_; }

private:
    void computeGrids();

    CliffRegion cliffs_;
    OverhangField overhangs_;
    ArchField archField_;
    double archFade_;
    std::vector<Arch> arches_;
    std::vector<float> control_;
    std::vector<float> weight_;
    HeightGrid surface_;
    std::vector<std::uint8_t> airBelowRock_;
};

// Columns of a chunk, sampled every spacing meters: kGridSamples x kGridSamples columns at
// (originX + i * spacing, originZ + k * spacing), with bounds of their rock surfaces.
struct SculptedChunkColumns {
    double spacing = kCellSizeMeters;
    std::vector<SculptedColumn> columns; // x varies fastest
    float minHeight = 0.0f;              // below every surface of the columns
    float maxHeight = 0.0f;              // above every surface of the columns

    [[nodiscard]] const SculptedColumn& at(int x, int z) const {
        return columns[static_cast<std::size_t>(z * kGridSamples + x)];
    }
};

[[nodiscard]] SculptedChunkColumns sampleSculptedColumns(const SculptedRegion& region,
                                                         double originX, double originZ,
                                                         double spacing = kCellSizeMeters);

// Density grid of the chunk whose lowest samples are at originY, with the same spacing
// vertically: sample (i, j, k) holds region.density(columns.at(i, k), originY + j * spacing).
[[nodiscard]] DensityGrid sculptedDensityFromColumns(const SculptedRegion& region,
                                                     const SculptedChunkColumns& columns,
                                                     double originY);

} // namespace terrain
