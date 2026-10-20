#pragma once

#include "terrain/generation/cave_noise.hpp"
#include "terrain/generation/overhangs.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace terrain {

// Lengths are in meters. The depth of a point is ground - y, below the ground of its column.
// A cave is a void: its signed distance V is negative inside it, and the caves are carved out
// of the terrain density with a smooth subtraction (carve). Wavelengths are one noise lattice
// cell of the first octave.

// Spaghetti caves: long winding tunnels along the zero lines shared by two 3D noises.
struct SpaghettiParams {
    double wavelength = 128.0;
    double verticalScale = 1.5; // flatter tunnels, more horizontal than vertical
    float minRadius = 3.0f;
    float maxRadius = 6.0f;
    double radiusWavelength = 64.0; // of the radius noise, shared with the noodles
};

// Noodle caves: the same tunnels, thin and dense, only where their presence noise is high.
struct NoodleParams {
    double wavelength = 48.0;
    double verticalScale = 1.5;
    float minRadius = 1.0f;
    float maxRadius = 1.8f;
    double presenceWavelength = 128.0;
    float presenceThreshold = 0.05f;
};

// Cheese caves: caverns with pillars where a 3D fbm rises above a threshold, deep enough.
struct CheeseParams {
    double wavelength = 96.0;
    int octaves = 2;
    float threshold = 0.32f;
    float minDepth = 40.0f;
};

// Ravines: tall narrow fissures along the zero lines of a 2D noise, in the areas where their
// presence fbm is high. Below the minimum roof, a V: maxHalfWidth wide at the top, closed
// height meters lower.
struct RavineParams {
    double wavelength = 500.0;
    double presenceWavelength = 1500.0;
    float presenceLow = 0.1f;
    float presenceHigh = 0.4f;
    float maxHalfWidth = 5.0f;
    float height = 60.0f;
};

// Surface entrances: at most one per cell of a world grid of spacing meters, at the center of
// the cell moved by up to jitter / 2 of the cell along each axis, kept with probability chance.
// A sloped tube runs from aboveGround meters over the ground of its site down to a chamber at
// a depth in [minDepth, maxDepth] below the site, run meters away horizontally.
struct EntranceParams {
    double spacing = 256.0;
    double jitter = 0.8;
    float chance = 0.35f;
    float minRun = 28.0f;
    float maxRun = 44.0f;
    float minDepth = 24.0f;
    float maxDepth = 48.0f;
    float minTubeRadius = 3.5f;
    float maxTubeRadius = 5.0f;
    float minChamberRadius = 7.0f;
    float maxChamberRadius = 10.0f;
    float aboveGround = 3.0f;
    // The ground along the entrance must stand this far above the sea: no entrance in the sea
    // or on the shore.
    float minShoreHeight = 4.0f;
};

struct CaveParams {
    SpaghettiParams spaghetti;
    NoodleParams noodles;
    CheeseParams cheese;
    RavineParams ravines;
    EntranceParams entrances;

    // Limits. Caves stay at least minRoof below the ground of their column (a vertical roof,
    // entrances aside) and at most maxDepth below it; none within riverClearance of a river
    // horizontally, and none where the sea is deeper than deepOceanDepth.
    float minRoof = 12.0f;
    float maxDepth = 96.0f;
    float riverClearance = 24.0f;
    float deepOceanDepth = 200.0f;

    // Carving: blend is the radius of the smooth subtraction, voids are clamped to
    // +-voidClamp, and beyond guard (CaveDetail) the carved density fades back to the terrain
    // density over fade meters of void distance.
    float blend = 2.0f;
    float fade = 2.0f;
    float voidClamp = 32.0f;
};

// Components of the caves, as bits of a CaveDetail.
enum CaveComponent : unsigned {
    kCaveSpaghetti = 1U << 0,
    kCaveNoodles = 1U << 1,
    kCaveCheese = 1U << 2,
    kCaveRavines = 1U << 3,
    kCaveEntrances = 1U << 4,
};
inline constexpr unsigned kAllCaveComponents =
    kCaveSpaghetti | kCaveNoodles | kCaveCheese | kCaveRavines | kCaveEntrances;

// Which components are carved, and the void distance from which the carved density fades back
// to the terrain density: a few cells of the lattice that is meshed, so that the walls and
// their normals only see the carved density.
struct CaveDetail {
    unsigned components = kAllCaveComponents;
    float guard = 4.0f;

    [[nodiscard]] bool has(CaveComponent component) const { return (components & component) != 0; }

    friend bool operator==(const CaveDetail&, const CaveDetail&) = default;
};

// What the caves read from the relief: the ground height of a column and, if there are
// rivers, the horizontal distance to the nearest one (empty: no river anywhere).
struct CaveGround {
    ReliefHeight height;
    ReliefHeight riverDistance;
};

// A surface entrance: a tube of radius tubeRadius from top (over the ground of the site) to
// bottom, the center of a chamber of radius chamberRadius.
struct CaveEntrance {
    double topX = 0.0;
    double topY = 0.0;
    double topZ = 0.0;
    double bottomX = 0.0;
    double bottomY = 0.0;
    double bottomZ = 0.0;
    float tubeRadius = 0.0f;
    float chamberRadius = 0.0f;
    int cellX = 0;
    int cellZ = 0;

    friend bool operator==(const CaveEntrance&, const CaveEntrance&) = default;
};

// At most this many entrances reach a column.
inline constexpr int kMaxColumnEntrances = 4;

// State of a column, computed once for all its samples.
struct CaveColumn {
    double x = 0.0;
    double z = 0.0;
    float ground = 0.0f;
    // Part of the limits that does not depend on y: max(riverClearance - river distance,
    // sea depth - deepOceanDepth). Caves are only carved where it is negative.
    float exclusion = 0.0f;
    float ravineDistance = 0.0f; // horizontal, to the axis of the nearest ravine
    float ravineStrength = 0.0f; // [0, 1], 0 outside the ravine areas
    // Entrances whose reach holds the column, as indices into the list it was built from.
    std::array<int, kMaxColumnEntrances> entrances{};
    int entranceCount = 0;
};

// Voids of the components at a point, meters, negative inside, clamped; voidClamp for a
// component that is not carved. limit is the void of the limits (positive where the caves are
// forbidden) and entranceLimit the one of the entrances (no roof).
struct CaveVoids {
    float spaghetti = 0.0f;
    float noodles = 0.0f;
    float cheese = 0.0f;
    float ravines = 0.0f;
    float entrances = 0.0f;
    float limit = 0.0f;
    float entranceLimit = 0.0f;
};

// Deterministic caves: the same seed, parameters, ground and position always give the same
// void, wherever the area being generated is. CaveSampler evaluates them over an area.
class CaveField {
public:
    explicit CaveField(std::uint64_t seed, const CaveParams& params = {});

    // Candidate entrance of the grid cell (cellX, cellZ); false if the cell has none: chance,
    // or its ground is under the sea or on the shore, a river is within its reach, or its
    // chamber would lie deeper than maxDepth or too close to the ground.
    [[nodiscard]] bool entrance(int cellX, int cellZ, const CaveGround& ground,
                                CaveEntrance& result) const;
    // Entrances whose reach meets the rectangle [minX, maxX] x [minZ, maxZ], in cell order.
    [[nodiscard]] std::vector<CaveEntrance> entrancesNear(double minX, double minZ, double maxX,
                                                          double maxZ,
                                                          const CaveGround& ground) const;
    // Horizontal reach of an entrance: columns farther from its axis ignore it.
    [[nodiscard]] double entranceReach(const CaveEntrance& entrance) const;
    // True if (x, z) lies within margin of the tube or the chamber of one of the entrances,
    // horizontally.
    [[nodiscard]] static bool nearEntrance(double x, double z,
                                           std::span<const CaveEntrance> entrances,
                                           double margin);

    // Column at (x, z) whose ground is ground, reached by entrances of the list.
    [[nodiscard]] CaveColumn column(double x, double z, float ground, const CaveGround& relief,
                                    std::span<const CaveEntrance> entrances) const;

    // Void of the limits at height y: max(minRoof - depth, depth - maxDepth, exclusion).
    [[nodiscard]] float limit(const CaveColumn& column, double y) const;
    // Noises read by the components of the detail.
    [[nodiscard]] unsigned noisesOf(const CaveDetail& detail) const;
    // Lower bound of the smallest void of the noise components of the detail (spaghetti,
    // noodles, cheese) for any noise values within [low, high].
    [[nodiscard]] float noiseLowerBound(const CaveNoiseValues& low, const CaveNoiseValues& high,
                                        const CaveDetail& detail) const;
    // Void of the ravines at height y of the column.
    [[nodiscard]] float ravineVoid(const CaveColumn& column, double y) const;
    // Voids of the components at height y of the column, the noises interpolated there.
    [[nodiscard]] CaveVoids voids(const CaveColumn& column, double y, const CaveNoiseValues& noise,
                                  std::span<const CaveEntrance> entrances,
                                  const CaveDetail& detail) const;
    // Void of the entrances reaching the column within their limits (no roof) at height y:
    // max(entrances, entranceLimit) of voids.
    [[nodiscard]] float limitedEntranceVoid(const CaveColumn& column, double y,
                                            std::span<const CaveEntrance> entrances) const;
    // Void of the caves: the components within their limits, clamped to +-voidClamp.
    [[nodiscard]] float combine(const CaveVoids& voids) const;
    // Terrain density with the void carved out: smoothSubtract(terrain, void, blend), faded
    // back to terrain between guard and guard + fade. Exactly terrain from guard + fade on;
    // never below terrain: the caves never add rock.
    [[nodiscard]] float carve(float terrain, float caveVoid, const CaveDetail& detail) const;

    [[nodiscard]] const CaveParams& params() const { return params_; }
    [[nodiscard]] const CaveNoiseLattice& lattice() const { return lattice_; }

private:
    [[nodiscard]] float entranceVoid(const CaveColumn& column, double y,
                                     std::span<const CaveEntrance> entrances) const;

    CaveParams params_;
    std::uint64_t entranceSeed_;
    std::uint64_t ravineSeed_;
    std::uint64_t ravinePresenceSeed_;
    CaveNoiseLattice lattice_;
};

} // namespace terrain
