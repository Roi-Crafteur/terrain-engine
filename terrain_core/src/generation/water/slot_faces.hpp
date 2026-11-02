#pragma once

// The faces of the rock of the slots of the falls (phase 12A), internal to terrain_core: from the
// wall of a slot at a column (slot_wall.hpp, all it reads of the valleys), a carved face
// (RockFace::carved) whose step turns the cliff of the slot into a vertical face from its foot to
// its rim, at the middle of the cliff of the slot (where the tiles of LOD 1 draw it: the wall moves
// by half a meter or so between the two, as the faces of the mountains). On a wall kNotchFrom
// meters high or more (all of it from kNotchFull) and kNotchWidth meters across, a notch along its
// foot, just above the water: an undercut of kNotchDeepSoft meters in soft rock to kNotchDeepHard
// in hard rock (Heightfield::rockHardness at notchHardnessHeight), kNotchHighSoft to
// kNotchHighHard meters high, varied along the wall by a noise of the place on rotated lattices
// (no row along any wall). It goes into the rock only as far as the wall reaches behind the rim
// (slot_wall.hpp: about 0.5 m today, as deep as drawn once the valleys hand it on 4 m behind).
// Drawn by the tiles of LOD 0 only: an undercut of 3 m at most is none from cells of 2 m on
// (rock_faces.cpp, drawnFeature, its thresholds unchanged), so the tiles of LOD 1 draw the wall
// without it. Slots narrower than kNotchWidth keep the cliff their valley draws where the tiles
// draw no face (rock_faces.hpp: kRunCells).

#include "terrain/generation/rock_faces.hpp"
#include "terrain/generation/water/slot_wall.hpp"
#include "terrain/world_size.hpp"

namespace terrain::water {

inline constexpr double kNotchFrom = 10.0;
inline constexpr double kNotchFull = 14.0;
inline constexpr double kNotchDeepSoft = 3.0;
inline constexpr double kNotchDeepHard = 2.7;
inline constexpr double kNotchHighSoft = 7.0;
inline constexpr double kNotchHighHard = 5.5;
// The tiles draw the overhangs of a face only over a run of 6 cells (rock_faces.hpp: kRunCells,
// their weight from 0.75): a notch only on walls kNotchWidth meters across.
inline constexpr double kNotchWidth = 6.0;

// Whether wall w is high and wide enough for a notch (the hardness of its rock decides it then).
[[nodiscard]] bool notched(const SlotWall& w);

// The height at which the hardness of the rock of wall w decides its notch: its middle.
[[nodiscard]] double notchHardnessHeight(const SlotWall& w);

// The carved face of wall w at the canonical point (x, z) of world, its rock of hardness there (in
// [0, 1], 0: soft; read only when notched(w)); of weight 0 without a wall.
[[nodiscard]] RockFace slotFace(const SlotWall& w, double x, double z, float hardness,
                                const WorldSize& world);

} // namespace terrain::water
