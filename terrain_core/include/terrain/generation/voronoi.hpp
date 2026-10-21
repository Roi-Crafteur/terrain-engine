#pragma once

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain {

// What a tectonic plate carries, before the local layers of the Heightfield.
enum class PlateKind : std::uint8_t {
    Continent,
    Ocean,
    InlandSea,        // water plate whose neighbors are all continents
    ContinentalShelf, // shallow submerged platform next to at least one continent
};

// Relative motion of two neighboring plates across their boundary.
enum class BoundaryKind : std::uint8_t {
    Collision,  // the plates move toward each other: mountain ranges, island arcs
    Divergence, // the plates move apart: rifts, mid-ocean ridges
    Transform,  // the plates slide past each other: no relief
};

// Lengths are in meters. Levels, uplift and rift depths are in units of the plate term, which
// the Heightfield scales by amplitude before adding it to the continentalness.
struct VoronoiParams {
    // Amplitude of the plate term in the continentalness of the Heightfield. 0 (the default)
    // disables the layer: the plates are neither built nor evaluated, so the world is
    // bit-identical to one without them. 0.5 gives planetary continents and oceans.
    float amplitude = 0.0f;

    // The number of plates is drawn from the seed in [minPlates, maxPlates]. They tile the
    // world (WorldSize), a rectangular torus: the mean equivalent diameter of a cell,
    // 2 sqrt(area / pi), is TectonicPlates::cellDiameter(), about 9,400 km on the default world
    // with 30 plates.
    int minPlates = 20;
    int maxPlates = 40;
    int lloydIterations = 2; // Lloyd relaxation of the sites; 0 disables it

    // Domain warping of the coordinates before the Voronoi lookup, so that the boundaries
    // bend instead of drawing raw polygons: a large fbm and a detail fbm, 3 octaves each.
    // With these values the warp never folds the plane (its Jacobian stays positive); from
    // about 1,300 km of large amplitude it does, cutting plates into enclaves.
    double warpWavelength = 5'000'000.0;
    double warpAmplitude = 1'000'000.0;
    double detailWarpWavelength = 1'000'000.0;
    double detailWarpAmplitude = 100'000.0;

    // Level of each plate kind, blended across the boundaries over coastWidth.
    float continentLevel = 1.0f;
    float oceanLevel = -1.0f;
    float inlandSeaLevel = -0.5f;
    float shelfLevel = -0.15f;

    // Coasts: half width of the blend between two plates, and fbm added to the level along
    // the boundaries between land and water, scaled by the coast factor (bays, peninsulas).
    double coastWidth = 350'000.0;
    double coastNoiseWavelength = 1'000'000.0;
    float coastRoughness = 0.6f;

    // Collisions: a belt of half width collisionWidth along the boundary, raising the level
    // by collisionUplift at its center and lifting the mountain mask of the Heightfield,
    // whose mountains are also collisionMountainBoost higher there (0.5 = +50%): collision
    // ranges are the highest ones.
    double collisionWidth = 400'000.0;
    float collisionUplift = 1.0f;
    float collisionMountainBoost = 0.5f;

    // Divergences: a trough of half width riftWidth and depth riftDepth, between raised
    // flanks of half width riftFlankWidth and height riftFlankHeight: a rift valley. Between
    // two water plates the flanks rise by oceanRidgeHeight instead: a mid-ocean ridge with
    // its axial rift.
    double riftWidth = 150'000.0;
    float riftDepth = 1.2f;
    double riftFlankWidth = 450'000.0;
    float riftFlankHeight = 0.4f;
    float oceanRidgeHeight = 0.6f;

    // Near a junction of three plates, the effects of a boundary fade out over this distance
    // into the third plate, so that they stop at the junction.
    double junctionFade = 150'000.0;
};

// One plate: its Voronoi site on the torus, its velocity (arbitrary units) and its kind.
struct Plate {
    double x = 0.0; // in the canonical range of the world
    double z = 0.0;
    double velocityX = 0.0;
    double velocityZ = 0.0;
    PlateKind kind = PlateKind::Ocean;
};

// Boundary between two neighboring plates a < b, i.e. an edge of the Voronoi diagram of the
// sites (before warping). convergence is the relative velocity of a toward b, projected on
// the unit vector from site a to site b and divided by its norm: in [-1, 1], the same seen
// from either plate. Collision above kCollisionThreshold, divergence below its opposite.
struct PlateBoundary {
    int a = 0;
    int b = 0;
    BoundaryKind kind = BoundaryKind::Transform;
    float convergence = 0.0f;
    double length = 0.0; // of the Voronoi edge, in meters
};

inline constexpr float kCollisionThreshold = 0.5f; // cos 60 degrees

// The plate layer at one point. The fields are continuous in (x, z), except plate, neighbor
// and boundaryDistance, which describe the closest boundary.
struct PlateSample {
    int plate = 0;                 // nearest plate after warping
    int neighbor = 0;              // second nearest plate, always a neighbor of plate
    double boundaryDistance = 0.0; // to the bisector of plate and neighbor, in warped meters
    float level = 0.0f;            // blended plate kind level, with the coast roughness
    float collision = 0.0f;        // [0, 1], collision belt times the boundary strength
    float rift = 0.0f;             // < 0 in the troughs, > 0 on the flanks
    float coast = 0.0f;            // [0, 1], 1 on a boundary between land and water
    float continentalness = 0.0f;  // level + collisionUplift * collision + rift
};

// Deterministic Voronoi layout of tectonic plates on the torus of the world: the same seed and
// parameters always give the same plates, boundaries and samples. Periodic on the world: the
// sites, the domain warping and the coast noise all tile it, and (x, z) is brought back into
// the canonical range first.
class TectonicPlates {
public:
    // Throws std::invalid_argument if minPlates < 4, maxPlates < minPlates or
    // maxPlates > kMaxPlates, lloydIterations < 0, a length is not finite and above 0, or the
    // world is invalid (validate).
    TectonicPlates(std::uint64_t seed, const VoronoiParams& params, const WorldSize& world = {});

    static constexpr int kMaxPlates = 256;

    [[nodiscard]] PlateSample sampleAt(double x, double z) const;

    // Coordinates after the domain warping, where the Voronoi diagram is evaluated (before
    // being brought back into the canonical range).
    struct Point {
        double x;
        double z;
    };
    [[nodiscard]] Point warp(double x, double z) const;

    // Nearest site to a point of the torus, without warping (the raw Voronoi diagram).
    [[nodiscard]] int rawPlateAt(double x, double z) const;

    // Index in boundaries() of the boundary between plates a and b, or -1 if they are not
    // neighbors.
    [[nodiscard]] int boundaryIndex(int a, int b) const;

    [[nodiscard]] const WorldSize& world() const { return world_; }
    // Mean equivalent diameter of a cell, 2 sqrt(world area / (pi plates)).
    [[nodiscard]] double cellDiameter() const { return cellDiameter_; }
    [[nodiscard]] const std::vector<Plate>& plates() const { return plates_; }
    [[nodiscard]] const std::vector<PlateBoundary>& boundaries() const { return boundaries_; }
    [[nodiscard]] const VoronoiParams& params() const { return params_; }

private:
    VoronoiParams params_;
    WorldSize world_;
    double cellDiameter_ = 0.0;
    std::vector<Plate> plates_;
    std::vector<PlateBoundary> boundaries_;
    std::vector<int> boundaryIndex_; // plates x plates, -1 when not neighbors
    // Large warp along x and z, detail warp along x and z, coast roughness: only the enabled
    // ones are built.
    std::array<noise::FbmOctaves, 4> warps_;
    noise::FbmOctaves coast_;
    double gateWidth_ = 0.0; // plates farther than this gap from a point do not affect it
};

} // namespace terrain
