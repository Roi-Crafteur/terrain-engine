#pragma once

// The depressions below the sea level of the planetary geography (phase 11B), internal to
// terrain_core: closed basins whose floors lie tens to hundreds of meters below the sea, deep
// inside the continents (the Qattara depression, the Turpan basin, Death Valley). The relief
// first: they are placed by the geography alone, and the hydrology fills them, or leaves them
// dry or salt, after the climate (phase 11B).
//
// At most one per cell of about kCell meters of the world, off its center, where the geography
// at the center is a low plain far from the sea, without belt or rift. Each is an irregular
// basin: elongated, its outline warped and lobed by noises, a flat floor over its inner part,
// gentle sides but for an escarpment on one of them (the Qattara depression). The Heightfield
// brings the ground toward the floor by the weight of the basin, only where the mask of the
// mountains is 0 (its weight vanishes before the intensity of the relief raises the mountains:
// they keep their bits), and never near the sea: dry land below the sea level never touches it.
//
// Pure arithmetic of doubles, noises and hashes: the same bits on every platform, periodic on
// the world.

#include "depression_forms.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain::depressions {

// The depressions of a world, found once.
struct Sites {
    WorldSize world;
    Forms forms;
    std::vector<Site> sites; // the depressions, cell by cell
    // On a lattice of kReachCell meters, 1 + the indices of the depressions that may reach a
    // cell (at most three: they lie at least 360 km apart and reach at most 165 km), 0 where none
    // does: elsewhere nothing to look for.
    int reachX = 1;
    int reachZ = 1;
    std::vector<std::array<std::uint16_t, 3>> reach;
};

// The sites of seed on the geography: one test of the geography per cell. A depression never
// reaches as far as the cell of another (they lie at least 360 km apart).
[[nodiscard]] Sites makeSites(std::uint64_t seed, const Geography& geography);

// Whether a depression may reach the canonical (x, z): false far from all of them (cheap).
[[nodiscard]] bool mayReach(const Sites& sites, double x, double z);

// The depression reaching the canonical (x, z) whose profile there goes the deepest (a share of 0
// without any), for ground base meters high, and the altitude of its floor.
struct Basin {
    Profile profile;
    double floor = 0.0;
};
[[nodiscard]] Basin basinAt(const Sites& sites, double x, double z, double base);

} // namespace terrain::depressions
