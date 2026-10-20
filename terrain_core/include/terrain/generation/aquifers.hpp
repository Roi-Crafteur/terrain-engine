#pragma once

#include "terrain/generation/cave_regions.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>

namespace terrain {

class CaveField;
struct CaveGround;

// Aquifers: the world is cut into the Voronoi cells of the sites of a jittered grid. The caves of
// a wet cell hold water up to the flat level of the cell; along the bisector between two cells,
// a barrier of rock closes the caves below the higher of their levels, so that the water of a
// cell never stands as a wall against the air or the lower water of its neighbor. Lengths are
// in meters.
struct AquiferParams {
    double cellSize = 192.0;
    // Sites move by up to jitter / 2 of a cell along each axis: at most 0.5, so that the
    // nearest site to a point is always one of the 3 x 3 cells around it.
    double jitter = 0.5;
    // Probability that a land cell is wet, from dryChance at dryHumidity (and below) to
    // wetChance at wetHumidity (and above). Cells under the sea are always wet.
    float dryChance = 0.1f;
    float wetChance = 0.6f;
    float dryHumidity = 0.2f;
    float wetHumidity = 0.8f;
    // Level: the lowest ground of the cell (the site and a 3 x 3 grid of probes over its grid
    // square), minus the minimum roof, minus a depth drawn in [minLevelDepth, maxLevelDepth];
    // rounded down to n + 0.5 so that no lattice sample lies on it. Under the higher ground of
    // the cell, the caves stand above the water.
    float minLevelDepth = 12.0f;
    float maxLevelDepth = 52.0f;
    // Depth of the level under the roof in a flooded cave region (CaveProfile::kFlooded).
    float floodedDepth = 1.0f;
    // Water freezes where the surface temperature is at most freezeTemperature.
    float freezeTemperature = 0.0f;
    // Barriers: rock within barrierHalfWidth of a bisector.
    float barrierHalfWidth = 3.0f;
    // Entrances stay this far above the levels of the cells around their site.
    float entranceMargin = 2.0f;
    // Water stays groundMargin below the ground, and its mesh goes skin into the rock so that
    // its contact with the rock is hidden.
    float groundMargin = 0.5f;
    float skin = 0.05f;
};

// Level of a dry aquifer: below everything.
inline constexpr float kDryLevel = -1.0e9f;

// An aquifer cell: its site, whether its caves hold water, at which level, and whether it is
// frozen; cold whether any water of the cell freezes (the lake, a river), wet or not.
struct AquiferCell {
    int cellX = 0;
    int cellZ = 0;
    double siteX = 0.0;
    double siteZ = 0.0;
    bool wet = false;
    bool frozen = false;
    float level = kDryLevel;
    bool cold = false;

    friend bool operator==(const AquiferCell&, const AquiferCell&) = default;
};

// A barrier reaching a column: rock below top where the bisector is less than width away from
// the edge of the barrier (width = barrierHalfWidth - distance to the bisector).
struct AquiferBarrier {
    float width = 0.0f;
    float top = kDryLevel;
};
inline constexpr int kMaxColumnBarriers = 8;

// Aquifer cells and cave regions read so far, kept: the same cells as CaveField::aquifer and
// CaveField::district, computed once. The field and the ground must outlive the cache.
class AquiferCache {
public:
    AquiferCache(const CaveField& field, const CaveGround& ground)
        : field_(&field), ground_(&ground) {}

    [[nodiscard]] const AquiferCell& at(int cellX, int cellZ);
    // Cell whose site is the nearest to (x, z): the aquifer of the column there.
    [[nodiscard]] const AquiferCell& nearest(double x, double z);

    // District of the cell (cellX, cellZ) of the cave regions, which must exist.
    [[nodiscard]] const CaveDistrict& district(int cellX, int cellZ);
    // Weights of the cave regions at (x, z) and their profile there (neutral without regions).
    [[nodiscard]] DistrictWeights districtWeights(double x, double z);
    [[nodiscard]] CaveProfile profileAt(double x, double z);
    // Highest water or barrier top that a column within reach of (x, z) can hold: the level of
    // the cell of (x, z) and, where a bisector lies within reach + barrierHalfWidth, the level
    // of the cell across it. kDryLevel if all of them are dry. reach is at most a few tens of
    // meters (the 3 x 3 cells around (x, z) are searched).
    [[nodiscard]] float highestLevelNear(double x, double z, double reach);
    // Highest top of the band of a river (CaveField::riverBandTop) that a column within reach
    // of (x, z) can hold; kDryLevel without rivers there, and always without cave regions.
    [[nodiscard]] float riverBandTopNear(double x, double z, double reach);

    // Lowest ground around the node (i, k) of the world lattice of the level of the galleries
    // (GalleryParams::levelSpacing): the lowest of 3 x 3 probes half a spacing apart around it.
    [[nodiscard]] double galleryNode(int i, int k);
    // The nodes interpolated at (x, z) with cubic Hermite weights of zero tangents: C1, and a
    // convex combination of the 4 nodes around (x, z), never outside their range.
    [[nodiscard]] double galleryLevel(double x, double z);
    // Flatness of the level at (x, z), in [0, 1]: 1 where the nodes around are flat enough for
    // a river (CaveRiverFlatness), 0 where they are too steep; interpolated as the level.
    [[nodiscard]] double galleryFlatness(double x, double z);

private:
    const CaveField* field_;
    const CaveGround* ground_;
    std::unordered_map<std::uint64_t, AquiferCell> cells_;
    std::unordered_map<std::uint64_t, CaveDistrict> districts_;
    [[nodiscard]] double flatnessBase(int i, int k);
    [[nodiscard]] double flatnessNode(int i, int k);

    std::unordered_map<std::uint64_t, double> galleryNodes_;
    std::unordered_map<std::uint64_t, double> flatnessBases_;
    std::unordered_map<std::uint64_t, double> flatnessNodes_;
};

} // namespace terrain
