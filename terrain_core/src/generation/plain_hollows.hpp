#pragma once

// The hollows of the plains of agent 52, internal to terrain_core: small closed hollows, far too
// many to list, so implicit: computed where they are sampled, one candidate at most per cell of a
// lattice of the world, its kind set by the climate before the relief (pre_climate.hpp) at its
// center, never by the biome:
// - kettles: round ponds 30 to 200 m across, 2 to 8 m deep, bowls, in the cold plains once under
//   the ice sheets (a mean temperature under 3 C, none above 8 C; not dry: P / (P + PET) over
//   0.3, none under 0.2), the prairie potholes of Canada, the moraines of the Baltic: up to 1.4
//   per square kilometer in the heart of their belt (cells of 768 m);
// - pans: flat-floored playas and pans 0.8 to 8 km across, 2 to 12 m deep, oval and lobed, in the
//   dry lands (P / (P + PET) under 0.15, none over 0.3), the Great Basin, the Kalahari, the sors
//   of Kazakhstan: up to one per 380 square kilometers in the heart of their belt (cells of
//   16 km), one per 13,000 elsewhere: almost nothing.
// The climate of the planet level and the hydrology fill them or leave them dry. A hollow is laid
// only where the geography at its center is a plain (intensity at most 0.11, no belt) 40 km from
// the sea at least, out of the excluded zone (agent 46, low_footprint.hpp). Out of their outlines
// not a bit changes. Pure arithmetic of doubles and hashes: the same bits on every platform,
// periodic on the world.

#include "low_footprint.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/lows.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain {
class PreClimate;
}

namespace terrain::lows {

struct Hollows {
    std::uint64_t seed = 0;
    double density = 1.0;
    const Geography* geography = nullptr; // held by the Heightfield
    const PreClimate* climate = nullptr;  // held with the sites
    Exclusion excluded;
    std::array<PeriodicCells, 2> kettles; // along x, then z
    std::array<PeriodicCells, 2> pans;
};

// The hollows of seed on the geography, their kinds read on its climate (both kept by pointer).
[[nodiscard]] Hollows makeHollows(std::uint64_t seed, const Geography& geography,
                                  const PreClimate& climate, double density, Exclusion excluded);

// The hollow at the canonical (x, z), the deepest there of those reaching it.
struct HollowShape {
    double depth = 0.0;   // of its floor under the ground around, meters
    double profile = 0.0; // [0, 1], the share of its depth there (1 on its floor, 0 out of it)
    double keep = 1.0;    // the share of the relief of the plains its floor keeps
    double flat = 0.0;    // [0, 1], the share of the detail its floor buries
};
[[nodiscard]] HollowShape hollowAt(const Hollows& hollows, double x, double z);

// The hollows whose center lies in the box (canonical meters, not wrapping), kettles then pans,
// each by cell row by row: kind, center, mean radius, reach, floor (below the ground around:
// minus the depth), long axis.
[[nodiscard]] std::vector<LowSite> hollowsIn(const Hollows& hollows, double minX, double minZ,
                                             double maxX, double maxZ);

} // namespace terrain::lows
