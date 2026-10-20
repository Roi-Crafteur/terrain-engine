#pragma once

#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_galleries.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/cave_noise.hpp"
#include "terrain/generation/cave_regions.hpp"
#include "terrain/generation/overhangs.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
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
    float chance = 0.5f;
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

// Speleothems: stalactites hanging from the ceilings and stalagmites rising from the floors,
// placed as objects (see speleothems.hpp): they never change the density. At most one candidate
// per cell of a world grid of spacing meters, moved by up to jitter / 2 of the cell, kept with
// probability chance scaled by the climate and the biome above (dryFactor under deserts,
// savannas and beaches, from dryFactor to 1 with the humidity between dryHumidity and
// wetHumidity). Each hangs from the ceiling of the highest void at least minSpan high under its
// candidate that is not open to the sky, and none stands in a column crossing the tube or the
// chamber of an entrance (within entranceMargin): a stalagmite stands under it with probability
// stalagmiteChance. Lengths are drawn up to maxLength, at most maxShare of the free height
// (down to the water of a lake), and the two never touch.
struct SpeleothemParams {
    double spacing = 4.0;
    double jitter = 0.8;
    float chance = 0.35f;
    float dryFactor = 0.25f;
    float dryHumidity = 0.2f;
    float wetHumidity = 0.8f;
    float minSpan = 3.0f;
    float stalagmiteChance = 0.7f;
    float minLength = 0.4f;
    float maxLength = 4.0f;
    float maxShare = 0.4f;
    float radiusPerLength = 0.22f; // radius at the base, per meter of length
    float iceTemperature = -2.0f;  // colder at the surface: ice
    float entranceMargin = 1.0f;
};

struct CaveParams {
    SpaghettiParams spaghetti;
    NoodleParams noodles;
    CheeseParams cheese;
    RavineParams ravines;
    EntranceParams entrances;
    AquiferParams aquifers;
    SpeleothemParams speleothems;
    CaveMaterialParams materials;
    GalleryParams galleries; // carved where the profile of the cave regions has galleries
    CaveRiverParams rivers;  // where it has rivers
    // Cave regions (cave_regions.hpp): none by default, and then every cave is the one of the
    // neutral profile.
    std::optional<CaveDistrictParams> districts;

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
    kCaveAquifers = 1U << 5,  // barriers and water
    kCaveGalleries = 1U << 6, // the galleries, with the banks and sills of their rivers
};
inline constexpr unsigned kAllCaveComponents = kCaveSpaghetti | kCaveNoodles | kCaveCheese |
                                               kCaveRavines | kCaveEntrances | kCaveAquifers |
                                               kCaveGalleries;

// Which components are carved, and the void distance from which the carved density fades back
// to the terrain density: a few cells of the lattice that is meshed, so that the walls and
// their normals only see the carved density.
struct CaveDetail {
    unsigned components = kAllCaveComponents;
    float guard = 4.0f;

    [[nodiscard]] bool has(CaveComponent component) const { return (components & component) != 0; }

    friend bool operator==(const CaveDetail&, const CaveDetail&) = default;
};

// What the caves read from the relief: the ground height of a column, if there are rivers the
// horizontal distance to the nearest one (empty: no river anywhere), and the environment of the
// surface above (empty: the default CaveEnvironment).
struct CaveGround {
    ReliefHeight height;
    ReliefHeight riverDistance;
    std::function<CaveEnvironment(double x, double z)> environment;
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
    // Aquifer of the column (the cell of the nearest site): its water level (kDryLevel if dry)
    // and whether it is frozen, and the barriers along the bisectors near the column.
    float waterLevel = kDryLevel;
    bool frozen = false;
    std::array<AquiferBarrier, kMaxColumnBarriers> barriers{};
    int barrierCount = 0;
    // Parameters of the caves of the cave regions blended at the column (neutral without them).
    CaveProfile profile;
    // Galleries, where the profile has galleries or rivers (else left as is): the horizontal
    // distance to the axis of the nearest one and the height of their floor at the column, and
    // where the river field reaches its threshold, the level of the river in a gallery (its
    // floor where the level is too steep for water); kDryLevel elsewhere.
    float galleryDistance = 1.0e9f;
    float galleryFloor = kDryLevel;
    float riverLevel = kDryLevel;
    // Whether the water of the aquifer cell of the column freezes, its lake or a river.
    bool cold = false;
    // Level of the lava of the aquifer cell of the column (kDryLevel without lava).
    float lavaLevel = kDryLevel;
};

// What a 4 m noise cell bounds, whatever the profile of a column in it: the smallest distances
// to the tunnel lines (meters), the largest radii before the profile scale, the largest
// presence of the noodles and the largest cheese noise.
struct CaveCellBound {
    float spaghettiDistance = 0.0f;
    float spaghettiRadius = 0.0f;
    float noodleDistance = 0.0f;
    float noodleRadius = 0.0f;
    float presenceHigh = 0.0f;
    float cheeseHigh = 0.0f;
};

// Voids of the components at a point, meters, negative inside, clamped; voidClamp for a
// component that is not carved. limit is the void of the limits (positive where the caves are
// forbidden), entranceLimit the one of the entrances (no roof) and barrier the one of the
// aquifer barriers (positive in their rock), which closes every void. bank is positive in the
// band of rock around a river, which closes the other components and the entrances, and sill
// positive where it closes the bottom of the gallery too (combine); the defaults leave every
// void as it is: no gallery, no bank, no sill.
struct CaveVoids {
    float spaghetti = 0.0f;
    float noodles = 0.0f;
    float cheese = 0.0f;
    float ravines = 0.0f;
    float entrances = 0.0f;
    float limit = 0.0f;
    float entranceLimit = 0.0f;
    float barrier = 0.0f;
    float galleries = 1.0e9f;
    float bank = -1.0e9f;
    float sill = -1.0e9f;
};

// Deterministic caves: the same seed, parameters, ground and position always give the same
// void, wherever the area being generated is. CaveSampler evaluates them over an area.
class CaveField {
public:
    explicit CaveField(std::uint64_t seed, const CaveParams& params = {});

    // Candidate entrance of the grid cell (cellX, cellZ); false if the cell has none: chance,
    // or its ground is under the sea or on the shore, a river is within its reach, its chamber
    // would lie deeper than maxDepth or too close to the ground, or it would reach down to the
    // water of an aquifer around it (entranceMargin).
    [[nodiscard]] bool entrance(int cellX, int cellZ, const CaveGround& ground,
                                AquiferCache& aquifers, CaveEntrance& result) const;
    [[nodiscard]] bool entrance(int cellX, int cellZ, const CaveGround& ground,
                                CaveEntrance& result) const;
    // Entrances whose reach meets the rectangle [minX, maxX] x [minZ, maxZ], in cell order.
    [[nodiscard]] std::vector<CaveEntrance> entrancesNear(double minX, double minZ, double maxX,
                                                          double maxZ, const CaveGround& ground,
                                                          AquiferCache& aquifers) const;
    [[nodiscard]] std::vector<CaveEntrance> entrancesNear(double minX, double minZ, double maxX,
                                                          double maxZ,
                                                          const CaveGround& ground) const;

    // Aquifer of the cell (cellX, cellZ) of the aquifer grid, the profile of the cave regions at
    // its site read from cells (or computed alone when null).
    [[nodiscard]] AquiferCell aquifer(int cellX, int cellZ, const CaveGround& ground,
                                      AquiferCache* cells = nullptr) const;

    // The cave regions, or nullptr without them.
    [[nodiscard]] const CaveDistricts* districts() const {
        return districts_ ? &*districts_ : nullptr;
    }
    // District of the cell (cellX, cellZ) of the grid of the cave regions (which must exist).
    [[nodiscard]] CaveDistrict district(int cellX, int cellZ, const CaveGround& ground) const;
    // Horizontal reach of an entrance: columns farther from its axis ignore it.
    [[nodiscard]] double entranceReach(const CaveEntrance& entrance) const;
    // True if (x, z) lies within margin of the tube or the chamber of one of the entrances,
    // horizontally.
    [[nodiscard]] static bool nearEntrance(double x, double z,
                                           std::span<const CaveEntrance> entrances,
                                           double margin);

    // Column at (x, z) whose ground is ground, reached by entrances of the list; its profile and,
    // when aquifer, its aquifer read from cells. Without cells: no aquifer, and the profile
    // computed alone.
    [[nodiscard]] CaveColumn column(double x, double z, float ground, const CaveGround& relief,
                                    std::span<const CaveEntrance> entrances,
                                    AquiferCache* cells = nullptr, bool aquifer = true) const;

    // Void of the limits at height y: max(minRoof - depth, depth - maxDepth, exclusion).
    [[nodiscard]] float limit(const CaveColumn& column, double y) const;
    // Noises read by the components of the detail.
    [[nodiscard]] unsigned noisesOf(const CaveDetail& detail) const;
    // What the noise values within [low, high] bound, whatever the profile.
    [[nodiscard]] CaveCellBound cellBound(const CaveNoiseValues& low, const CaveNoiseValues& high,
                                          const CaveDetail& detail) const;
    // Lower bound of the smallest void of the noise components of the detail (spaghetti,
    // noodles, cheese) in a cell of these bounds, for a column of this profile.
    [[nodiscard]] float noiseLowerBound(const CaveCellBound& bound, const CaveProfile& profile,
                                        const CaveDetail& detail) const;
    [[nodiscard]] float noiseLowerBound(const CaveNoiseValues& low, const CaveNoiseValues& high,
                                        const CaveDetail& detail) const;
    // Void of the ravines at height y of the column.
    [[nodiscard]] float ravineVoid(const CaveColumn& column, double y) const;
    // Void of the galleries at height y of the column: an ellipse of halfWidth by halfHeight
    // scaled by the field of the profile around their axis, its floor at galleryFloor, fading
    // into the rock under presenceOn; voidClamp without galleries.
    [[nodiscard]] float galleryVoid(const CaveColumn& column, double y) const;
    // Rock of the band around a river (CaveRiverParams): positive within shell of the gallery
    // horizontally, from shell under its floor to freeboard over the deepest water, where the
    // river field closes it (bankLow to bankHigh); -voidClamp elsewhere. The sill is the same
    // band where the field is within sillWidth of the threshold of the water.
    [[nodiscard]] float bankVoid(const CaveColumn& column, double y) const;
    [[nodiscard]] float sillVoid(const CaveColumn& column, double y) const;
    // Top of the band of the rivers of a column (kDryLevel where the field has no band).
    [[nodiscard]] float riverBandTop(const CaveColumn& column) const;
    // Horizontal distances from (x, z) to the axes of the two networks of galleries (A, B):
    // galleryDistance of a column is the smaller one.
    [[nodiscard]] std::array<double, 2> galleryDistances(double x, double z) const;
    // Voids of the components at height y of the column, the noises interpolated there.
    [[nodiscard]] CaveVoids voids(const CaveColumn& column, double y, const CaveNoiseValues& noise,
                                  std::span<const CaveEntrance> entrances,
                                  const CaveDetail& detail) const;
    // Void of the entrances reaching the column within their limits (no roof) at height y:
    // max(entrances, entranceLimit) of voids.
    [[nodiscard]] float limitedEntranceVoid(const CaveColumn& column, double y,
                                            std::span<const CaveEntrance> entrances) const;
    // Void of the caves: the components within their limits, clamped to +-voidClamp. The bank
    // closes the components but the galleries and the entrances, the sill closes the galleries
    // too.
    [[nodiscard]] float combine(const CaveVoids& voids) const;
    // Density of the water of the column at height y, negative in the water: the carved air
    // (carved >= 0) below the level of its aquifer and groundMargin below the ground (terrain),
    // extended skin into the rock; with a river, also the air of the gallery (galleryVoid) below
    // the river level. voidClamp where the column holds no water.
    [[nodiscard]] float waterDensity(const CaveColumn& column, double y, float terrain,
                                     float carved) const;
    // Level of the water that may stand at height y of the column: the level of its aquifer
    // and, in a gallery, the level of its river; kDryLevel without water.
    [[nodiscard]] float waterLevelAt(const CaveColumn& column, double y) const;
    // Density of the lava of the column, as the water of a lake: negative in the carved air
    // under its level; voidClamp without lava.
    [[nodiscard]] float lavaDensity(const CaveColumn& column, double y, float terrain,
                                    float carved) const;
    // Terrain density with the void carved out: smoothSubtract(terrain, void, blend), faded
    // back to terrain between guard and guard + fade. Exactly terrain from guard + fade on;
    // never below terrain: the caves never add rock.
    [[nodiscard]] float carve(float terrain, float caveVoid, const CaveDetail& detail) const;

    [[nodiscard]] const CaveParams& params() const { return params_; }
    [[nodiscard]] const CaveNoiseLattice& lattice() const { return lattice_; }

private:
    void addAquifer(CaveColumn& column, AquiferCache& aquifers) const;
    void addGallery(CaveColumn& column, AquiferCache& cells) const;
    // Highest level of the lava of the cell (cellX, cellZ): under the band of the rivers of
    // every gallery its columns may hold.
    [[nodiscard]] double lavaCeiling(int cellX, int cellZ, const CaveGround& ground,
                                     AquiferCache* cells) const;
    [[nodiscard]] float barrierVoid(const CaveColumn& column, double y) const;
    [[nodiscard]] float entranceVoid(const CaveColumn& column, double y,
                                     std::span<const CaveEntrance> entrances) const;

    CaveParams params_;
    std::uint64_t entranceSeed_;
    std::uint64_t ravineSeed_;
    std::uint64_t ravinePresenceSeed_;
    std::uint64_t aquiferSeed_;
    std::uint64_t galleryASeed_;
    std::uint64_t galleryBSeed_;
    CaveNoiseLattice lattice_;
    std::optional<CaveDistricts> districts_;
};

} // namespace terrain
