#pragma once

// The stages of the construction of the planetary geography, internal to terrain_core: the
// crust and its coasts, the ocean floor, the drained regional elevation.

#include "geography_grid.hpp"
#include "terrain/generation/geography_params.hpp"

#include <cstdint>
#include <vector>

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

// The floor of the ocean from the distance to the coast: shelf, slope, abyssal plain, with the
// mid-ocean ridges and trenches of the plates; at most the shelf depth, which it is under the
// land.
[[nodiscard]] std::vector<float> oceanFloor(const GeographyParams& params, const Crust& crust,
                                            const Tectonics& tectonics);

// The lowest regional elevation each land node may have before the drainage: the continental
// tilt from the coast, the uplift of the belts and plateaus, the rift shoulders and troughs; at
// least 1 m.
[[nodiscard]] std::vector<float> landFloor(const GeographyParams& params, const Crust& crust,
                                           const Tectonics& tectonics);

// The drained regional elevation (a priority flood, 8-connected, from the land nodes on the
// coast): each land node gets max(floor, its parent + drainSlope * distance), so that every
// land node has a strictly lower path to the sea. 1 m over the sea.
[[nodiscard]] std::vector<float> drainedLand(const GeographyParams& params, const Grid& grid,
                                             const Crust& crust, const std::vector<float>& floor);

} // namespace terrain::geography
