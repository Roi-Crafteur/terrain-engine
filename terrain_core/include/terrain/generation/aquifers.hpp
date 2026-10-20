#pragma once

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
// frozen.
struct AquiferCell {
    int cellX = 0;
    int cellZ = 0;
    double siteX = 0.0;
    double siteZ = 0.0;
    bool wet = false;
    bool frozen = false;
    float level = kDryLevel;

    friend bool operator==(const AquiferCell&, const AquiferCell&) = default;
};

// A barrier reaching a column: rock below top where the bisector is less than width away from
// the edge of the barrier (width = barrierHalfWidth - distance to the bisector).
struct AquiferBarrier {
    float width = 0.0f;
    float top = kDryLevel;
};
inline constexpr int kMaxColumnBarriers = 8;

// Aquifer cells read so far, kept: the same cells as CaveField::aquifer, computed once. The
// field and the ground must outlive the cache.
class AquiferCache {
public:
    AquiferCache(const CaveField& field, const CaveGround& ground)
        : field_(&field), ground_(&ground) {}

    [[nodiscard]] const AquiferCell& at(int cellX, int cellZ);
    // Cell whose site is the nearest to (x, z): the aquifer of the column there.
    [[nodiscard]] const AquiferCell& nearest(double x, double z);
    // Highest water or barrier top that a column within reach of (x, z) can hold: the level of
    // the cell of (x, z) and, where a bisector lies within reach + barrierHalfWidth, the level
    // of the cell across it. kDryLevel if all of them are dry. reach is at most a few tens of
    // meters (the 3 x 3 cells around (x, z) are searched).
    [[nodiscard]] float highestLevelNear(double x, double z, double reach);

private:
    const CaveField* field_;
    const CaveGround* ground_;
    std::unordered_map<std::uint64_t, AquiferCell> cells_;
};

} // namespace terrain
