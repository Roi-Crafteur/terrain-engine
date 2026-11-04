#pragma once

// The plates under the seas (agent 51), internal to terrain_core: the mid-ocean ridges of the
// geography, the warp of its plates at the nodes of its map, the ridge nearest to each node of
// the sea through the sea and the distance to it, the trenches of the collisions and how active
// each margin is. Built once from the stages of the geography as it builds them
// (settlement::GeographyPlates, geography::buildTectonics: the same plates, boundaries and
// fields to the bit), then read at every point of the sea. The planet level never reads it.

#include "geography_grid.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {
class Geography;
}

namespace terrain::sea_plates {

// A mid-ocean ridge: the divergence between plates a and b, its axis the bisector of their
// sites in the warped plane of the plates; or the collision of a trench (Plates::trenches), the
// same geometry.
struct Ridge {
    int boundary = -1; // its index in TectonicPlates::boundaries()
    double ax = 0.0;   // the site of a (canonical)
    double az = 0.0;
    double mx = 0.0; // the middle of the sites (canonical), the origin along the axis
    double mz = 0.0;
    double nx = 1.0; // the unit vector from a to b: across the axis
    double nz = 0.0;
    float strength = 0.0f; // geography::Boundary::strength
    float rate = 0.0f;     // [0, 1]: the share of the fastest spreading
    float toward = 1.0f;   // a collision: +1 when b overrides a, -1 when a overrides b
};

inline constexpr float kFar = 1.0e9f; // the distance of a node no ridge reaches through the sea
inline constexpr std::int16_t kLand = -2; // the label of a node on land

struct Plates {
    geography::Grid grid;
    WorldSize world;
    double anchorX = 0.0;
    std::vector<float> offsetX; // the warp of the plates at each node, meters
    std::vector<float> offsetZ;
    std::vector<std::int16_t> label; // the nearest ridge through the sea, -1 for none, kLand
    std::vector<float> distance;     // to its axis through the sea, meters (kFar for none)
    std::vector<float> trench;       // [0, 1], geography::Tectonics::trench
    std::vector<float> belt;         // [0, 1], geography::Tectonics::belt (its guard)
    std::vector<float> active;       // [0, 1], a trench within reach: an active margin
    std::vector<Ridge> ridges;
    // The trenches: the collision of the trench field of each node (-1 for none), where its axis
    // lies across the boundary (meters toward the overriding plate, as the geography puts it) and
    // the strength of the trench along it (the trench field at its peak nearby).
    std::vector<std::int16_t> trenchLabel;
    std::vector<float> trenchAxis;
    std::vector<float> trenchPeak;
    std::vector<Ridge> trenches;

    [[nodiscard]] std::size_t memoryBytes() const;
};

// The plates of the geography of geo, whose Heightfield has the seed worldSeed, on up to threads
// threads (0: all the cores): the same bits whatever their number.
[[nodiscard]] Plates build(std::uint64_t worldSeed, const Geography& geo, int threads);

// A point of the warped plane of the plates (canonical).
struct Point {
    double x = 0.0;
    double z = 0.0;
};
// Where the canonical point (x, z) of the world lies in the warped plane (the warp of the nodes
// read bilinearly, as settlement::GeographyPlates::nearAt).
[[nodiscard]] Point warpedAt(const Plates& p, double x, double z);

// A warped point seen from a ridge: v the signed distance to its axis (negative on the side of
// plate a), u along the axis from the middle of the sites. Constant directions: no phase of a
// position times a turning direction.
struct Frame {
    double u = 0.0;
    double v = 0.0;
};
[[nodiscard]] Frame frameOf(const Ridge& r, const Point& q, const WorldSize& world);

// The 4 nodes around the canonical point (x, z) and their bilinear weights (summing to 1).
struct Corners {
    std::array<std::size_t, 4> node{};
    std::array<double, 4> weight{};
};
[[nodiscard]] Corners cornersAt(const Plates& p, double x, double z);

// A field of the nodes at the canonical point (x, z): cubic B-spline (smooth, C2).
[[nodiscard]] double splineAt(const Plates& p, const std::vector<float>& field, double x,
                              double z);

} // namespace terrain::sea_plates
