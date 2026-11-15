#pragma once

// The marginal plateaus of the seas (agent 51), internal to terrain_core: drowned blocks of
// continent off the passive margins (the Exmouth, Voring or Blake plateaus of the Earth), lobes
// raised over the slope, the rise and the deep floor. Drawn once, cell by cell of kCell meters:
// each cell holds one by chance, its middle anywhere in the cell, kept at sea on a passive margin
// at its distance from the large lands (SeaParams::marginal*); its size, its stretch, its turn,
// the bends of its outline, the depth and tilt of its top and its flanks drawn each, so that no
// two look alike and no row repeats. A point reads the plateaus listed in its cell. Deterministic,
// periodic on the world, the same whatever the threads.

#include "sea_plates.hpp"
#include "sea_shore.hpp"
#include "terrain/generation/seas.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain::sea_plateaus {

inline constexpr double kCell = 131'072.0; // divides every side of a world
// The elevation of a point no plateau reaches.
inline constexpr double kNone = -1.0e9;

struct Plateau {
    double x = 0.0; // its middle, canonical
    double z = 0.0;
    double radius = 0.0;  // meters across its short axis, from the middle to the outline
    double stretch = 1.0; // its long axis over its short one
    double cosine = 1.0;  // the direction of its long axis
    double sine = 0.0;
    // The bends of the outline: amplitudes of cos(k angle + phase) for k = 1, 2, 3.
    std::array<double, 3> bend{};
    std::array<double, 3> bendCos{};
    std::array<double, 3> bendSin{};
    double top = 0.0;       // depth of its top at the middle, meters (positive)
    double drop = 0.0;      // how much deeper its seaward edge than its landward one
    double shallowest = 0.0; // never shallower than this
    double seaX = 1.0;      // the unit vector away from the large land
    double seaZ = 0.0;
    double flank = 0.0;     // the fall of its flanks, meters per meter
    double reach = 0.0;     // meters from the middle beyond which it is below every floor
};

struct Plateaus {
    WorldSize world;
    int cellsX = 0;
    int cellsZ = 0;
    std::vector<Plateau> list; // in the order of their cells
    // The plateaus reaching each cell: those of cell c are members[start[c]] to
    // members[start[c + 1]] (indices into list, in its order).
    std::vector<std::uint32_t> start;
    std::vector<std::uint32_t> members;

    [[nodiscard]] std::size_t memoryBytes() const;
};

// The plateaus of the seas of seed seed on the margins of shore, passive by the active field of
// plates.
[[nodiscard]] Plateaus build(std::uint64_t seed, const WorldSize& world, const SeaParams& p,
                             const sea_shore::Shore& shore, const sea_plates::Plates& plates);

// The elevation of plateau q at the canonical point (x, z) of world: its top inside its outline,
// its flank falling beyond (meters, negative), kNone beyond its reach.
[[nodiscard]] double elevationOf(const Plateau& q, const WorldSize& world, double x, double z);

// The elevation of the highest plateau at the canonical point (x, z), kNone where none reaches.
[[nodiscard]] double elevationAt(const Plateaus& s, double x, double z);

} // namespace terrain::sea_plateaus
