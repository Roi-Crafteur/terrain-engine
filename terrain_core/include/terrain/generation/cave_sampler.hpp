#pragma once

#include "terrain/generation/cave_noise.hpp"
#include "terrain/generation/caves.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

// The caves of a field over a box of the world (a tile, a slice, a point): the entrances that
// reach the box and the noise nodes it reads are computed once. Every result is the same as
// from any other sampler holding the point, so areas sampled apart join without a seam.
// Not thread-safe (the node cache fills up on use): one sampler per thread.
class CaveSampler {
public:
    // The field and the ground must outlive the sampler.
    CaveSampler(const CaveField& field, const CaveGround& ground, const CaveDetail& detail,
                const CaveBox& box);

    // Column at (x, z), inside the box, whose ground height is ground.
    [[nodiscard]] CaveColumn column(double x, double z, float ground) const;

    // Terrain density terrain at height y of the column with the caves carved out
    // (CaveField::carve). Exactly terrain where the caves cannot reach: outside their limits,
    // or in a noise cell whose nodes keep every void beyond guard + fade, which skips the
    // interpolation of the noises.
    [[nodiscard]] float density(const CaveColumn& column, double y, float terrain);

    // Density of the water of the aquifer of the column (CaveField::waterDensity), negative in
    // the water; voidClamp without aquifers in the detail.
    [[nodiscard]] float waterDensity(const CaveColumn& column, double y, float terrain,
                                     float carved) const;

    // Voids of the components and their combination at height y of the column.
    [[nodiscard]] CaveVoids voids(const CaveColumn& column, double y);
    [[nodiscard]] float voidAt(const CaveColumn& column, double y);

    [[nodiscard]] const std::vector<CaveEntrance>& entrances() const { return entrances_; }
    [[nodiscard]] const CaveDetail& detail() const { return detail_; }
    [[nodiscard]] const CaveField& field() const { return *field_; }
    [[nodiscard]] const CaveGround& ground() const { return *ground_; }
    // Aquifer cell of the nearest site to (x, z) (the cell of the column there).
    [[nodiscard]] const AquiferCell& aquiferAt(double x, double z) const {
        return aquifers_.nearest(x, z);
    }
    // Profile of the cave regions at (x, z) (neutral without them) and their weights there.
    [[nodiscard]] CaveProfile profileAt(double x, double z) const {
        return aquifers_.profileAt(x, z);
    }
    [[nodiscard]] DistrictWeights districtWeights(double x, double z) const {
        return aquifers_.districtWeights(x, z);
    }

private:
    // What the noise cell holding (x, y, z) bounds, computed once per cell.
    [[nodiscard]] const CaveCellBound& cellBound(double x, double y, double z);

    const CaveField* field_;
    const CaveGround* ground_;
    CaveDetail detail_;
    unsigned noises_;
    // Aquifer cells and cave regions, filled by column(), which is const for its callers: the
    // cells never change a result.
    mutable AquiferCache aquifers_;
    std::vector<CaveEntrance> entrances_;
    CaveNoiseCache cache_;
    std::vector<CaveCellBound> cellBounds_;
    std::vector<std::uint8_t> boundKnown_;
};

// Density at (x, y, z) of the relief of ground (y - height, as a float) with the caves of the
// detail carved out, from a sampler of the point alone.
[[nodiscard]] float caveDensityAt(const CaveField& field, const CaveGround& ground,
                                  const CaveDetail& detail, double x, double y, double z);

} // namespace terrain
