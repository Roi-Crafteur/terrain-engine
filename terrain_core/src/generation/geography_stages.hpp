#pragma once

// The stages of the construction of the planetary geography, internal to terrain_core: the
// crust and its coasts, the ocean floor, the drained regional elevation.

#include "geography_grid.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/geography_params.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain {

// The noises of the geography at a point: the three coast stages and the ruggedness of the
// coasts, the island arcs, the warp of the valleys (twins).
struct Geography::Noises {
    noise::FbmOctaves coarse;
    noise::FbmOctaves detail;
    noise::FbmOctaves fine;
    noise::FbmOctaves rugged;
    noise::FbmOctaves arc;
    noise::FbmOctaves valleyX;
    noise::FbmOctaves valleyZ;
    bool twinValleys = false;
};

} // namespace terrain

namespace terrain::geography {

// Independent streams of the seed of the geography. New streams go at the end.
enum Stream : std::uint64_t {
    kCrust,
    kCoastCoarse,
    kCoastDetail,
    kCoastFine,
    kPlates,
    kProvinces,
    kCoastRugged,
    kArcs,
    kValleyGeometry,
    kValleyWarpX,
    kValleyWarpZ,
    kDrainage,
};

// The continental crust on the map: which nodes are land, and the signed distance from each
// node to the coast (meters, positive on land).
struct Crust {
    std::vector<std::uint8_t> land;
    std::vector<float> coast;
    int candidate = 0;
    int continents = 0; // land masses of at least params.continentArea
};

// The crust of the first candidate stream that gives the wanted continents (else the closest),
// with its small inner waters filled and its coast distance. The noise of the nodes is computed
// in blocks of rows on threads threads (planet::parallelRows): the same bits for any number.
[[nodiscard]] Crust buildCrust(std::uint64_t seed, const GeographyParams& params,
                               const WorldSize& world, const Grid& grid, int threads);

// What the plates do to each node of the map, from their boundaries nearby: the strongest of
// each effect over the boundaries, faded beyond the junctions of their edges.
struct Tectonics {
    std::vector<float> belt;   // [0, 1], the mountain belts of the collisions
    std::vector<float> uplift; // meters: the regional uplift of the belts and high plateaus
    std::vector<float> trough;   // [0, 1], the troughs of the continental rifts
    std::vector<float> shoulder; // [0, 1], their shoulders
    std::vector<float> ridge;    // [0, 1], the mid-ocean ridges along every divergence
    std::vector<float> trench; // [0, 1], the trenches on the subducting side of collisions
};

// The plates of the geography (TectonicPlates of params.plates) on the map: for each node, its
// warped position, the nearest plates and the boundaries between them. In a collision the plate
// with more land of the crust overrides the other. Node loops on threads in blocks of rows.
[[nodiscard]] Tectonics buildTectonics(std::uint64_t seed, const GeographyParams& params,
                                       const WorldSize& world, const Grid& grid,
                                       const Crust& crust, int threads);

// The provinces of the land between the belts (GeographyParams): what each node gets from the
// province nearest to it (after a warp), blended over provinceBlend.
struct Provinces {
    std::vector<float> uplift; // meters, added to the floor of the land
    std::vector<float> hills;  // meters, the amplitude of the hills
    std::vector<float> massif; // [0, 1], the low mountains of old massifs and high relief
};

// Sites on a jittered grid, each of a kind drawn from its shares (those of the forelands within
// forelandDistance of a belt), its uplift and hills drawn in the ranges of its kind.
[[nodiscard]] Provinces buildProvinces(std::uint64_t seed, const GeographyParams& params,
                                       const WorldSize& world, const Grid& grid,
                                       const Tectonics& tectonics, int threads);

// The floor of the ocean from the distance to the coast: shelf, slope, abyssal plain, with the
// mid-ocean ridges and trenches of the plates; at most the shelf depth, which it is under the
// land.
[[nodiscard]] std::vector<float> oceanFloor(const GeographyParams& params, const Crust& crust,
                                            const Tectonics& tectonics);

// The lowest regional elevation each land node may have before the drainage: the continental
// tilt from the coast, the uplift of the belts and plateaus, the rift shoulders and troughs, the
// uplift of the provinces (fading under the belts); at least 1 m.
[[nodiscard]] std::vector<float> landFloor(const GeographyParams& params, const Crust& crust,
                                           const Tectonics& tectonics,
                                           const Provinces& provinces);

// The steps from a node to its 8 neighbors, in opposite pairs (k ^ 1 is the opposite of k).
inline constexpr std::array<std::array<int, 2>, 8> kSteps = {
    {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}}};

// The drained regional elevation and the tree of its drainage, toward the sea.
struct Drainage {
    std::vector<float> elevation; // meters, at least 1; 1 over the sea
    std::vector<std::int8_t> down; // the step (kSteps) to the node downstream, -1 over the sea
    std::vector<float> area;       // square meters drained through each land node
};

// A priority flood, 8-connected, from the land nodes on the coast, in the order of their
// elevations jittered by up to drainJitter (drawn from the seed): each land node gets max(floor,
// its parent + drainSlope * distance), so that every land node has a strictly lower path to the
// sea, and drains to that parent; a land node on the coast drains to its first neighbor at sea.
[[nodiscard]] Drainage drainedLand(std::uint64_t seed, const GeographyParams& params,
                                   const Grid& grid, const Crust& crust,
                                   const std::vector<float>& floor);

// The polyline of the valley of each node toward the node downstream, kValleyFloats floats per
// node: the offsets of its start and of its middle from the position of the node (meters) and
// the scale of its floor, drawn from the seed. Its end is the start of the node downstream.
inline constexpr std::size_t kValleyFloats = 5;
[[nodiscard]] std::vector<float> valleyGeometry(std::uint64_t seed, const Grid& grid,
                                                const Drainage& drainage);

} // namespace terrain::geography
