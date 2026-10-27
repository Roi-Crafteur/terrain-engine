#pragma once

// The forms of the depressions below the sea level (depressions.hpp), internal to terrain_core:
// the profile of a basin from its rim down to its floor, and the forms of its floor.
//
// Its gentle sides go down over most of the way to the floor. On its steep side, along part of
// its length, a cliff of 80 to 200 m drops from the rim (its face 30 to 60 degrees steep, a talus
// of scree at its foot), buttes stand detached before the foot, and the ground goes on down more
// gently to the floor. The Heightfield carves the ravines of its relief down the sides (deep
// notches in the cliff, small vallons on the gentle slopes) within the room the profile leaves. Its
// floor keeps a share of the relief of the plains, with flat salt pans of a few kilometers and a
// few buttes of 20 to 80 m.
//
// Pure arithmetic of doubles and noises: the same bits on every platform, periodic on the world.

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::depressions {

// A depression: its center (canonical), its mean radius (meters), the ratio of its axes (at
// least 1) and the direction of the long one (turns from +x toward +z), the altitude of its
// floor (below 0), how much nearer its rim lies on its steep side and the direction of that side
// (turns).
struct Site {
    double x = 0.0;
    double z = 0.0;
    double radius = 1.0;
    double elongation = 1.0;
    double angle = 0.0;
    double floor = -1.0;
    double escarpment = 0.0;
    double steepSide = 0.0;
};

struct Forms {
    noise::FbmOctaves warpX;    // the warp of the space of the basins
    noise::FbmOctaves warpZ;
    noise::FbmOctaves outline;  // the lobes of their rims
    noise::FbmOctaves cliffs;   // where the steep side is a cliff
    noise::FbmOctaves faces;    // how steep its faces are
    noise::FbmOctaves edge;     // the spurs and re-entrants of its edge
    noise::FbmOctaves heights;  // how high the cliffs and the buttes of the floor are
    noise::FbmOctaves outliers; // the buttes detached before the foot of the cliffs
    noise::FbmOctaves pans;     // the salt pans of the floor
    noise::FbmOctaves buttes;   // the buttes of the floor
};

[[nodiscard]] Forms makeForms(std::uint64_t seed, const WorldSize& world);

// How the ground of a depression lies at a point: the share of the way down to its floor it goes
// (1 on the floor, 0 beyond the rim), the slope of that descent (meters per meter, up toward the
// rim along (upX, upZ), a unit direction), how much the point lies on the floor, and how deep the
// ravines may cut there (meters).
struct Profile {
    double share = 0.0;
    double slope = 0.0;
    double upX = 1.0;
    double upZ = 0.0;
    double onFloor = 0.0;
    double room = 0.0;
};

// The profile of s at the canonical (x, z), (dx, dz) from its center, on ground above meters above
// its floor (at least a meter).
[[nodiscard]] Profile profileOf(const Forms& forms, const Site& s, double x, double z, double dx,
                                double dz, double above);

// The forms of the floor at (x, z): how much it lies in a salt pan (flat, kPanDepth below the
// floor), in [0, 1], and the height of the buttes, meters (0 between them).
inline constexpr double kPanDepth = 2.0;
struct Floor {
    double pan = 0.0;
    double butte = 0.0;
};
[[nodiscard]] Floor floorAt(const Forms& forms, double x, double z);

} // namespace terrain::depressions
