#pragma once

// The wall of the slot of a fall at a column, as the valleys of the rivers tell it to the rock
// (phase 12A, commit 2): 32 bytes, the only thing the faces of the slots (water/slot_faces) read
// of the valleys. Its only maker is water::slotWallAt, called where the valley of a segment
// (water::segmentValley) draws the walls of a slot; the valley the ground follows at the column
// hands it on (Valley::wall, RiverCarve::wall). The valleys may be computed otherwise (the
// valleys of agent 31) as long as they fill it the same way:
// - the wall is the cliff of the slot as its valley draws it (2.5D), width meters across at most,
//   never over the channel: within width / 2 of its middle a face turns its climb into a step,
//   vertical at the middle, the rock below top behind it and below foot in front of it;
// - foot and top are the heights of the valley at the two edges of that width (the step meets the
//   ground there);
// - weight fades the face out where the valley is not that of the slot (the ends of the
//   corridors of the falls).
// Without a wall (width 0) the rock of the column is that of its relief.
//
// How far behind the rim it reaches decides how deep the notch of a wall goes (slot_faces.hpp):
// the valleys hand it on only where they look for their surface, about half a meter beyond the
// rim of a slot today, so a notch shows about 0.5 m deep at LOD 0 (up to 3 m where a slot is wide
// enough). For the valleys of agent 31: filling it up to 4 m behind the rim (the width of a face
// within kWallWidth, river_slot.hpp) lets the notches go as deep as slot_faces draws them, 2.7 to
// 3 m, with nothing to change in slot_faces nor in where the faces stand (the middle of the cliff
// of the slot, as the tiles of LOD 1 draw it).

namespace terrain {

struct SlotWall {
    float distance = 0.0f; // meters from the middle of the wall to the column, positive to the air
    float width = 0.0f;    // across the wall (0: no wall at the column)
    float foot = 0.0f;     // heights of the valley at the edges of the width, meters
    float top = 0.0f;
    float water = 0.0f;    // the level of the water at the foot of the wall
    float airX = 0.0f;     // unit horizontal direction from the rock of the wall to the air
    float airZ = 0.0f;
    float weight = 0.0f;   // in [0, 1]: how much the valley is that of the slot there

    friend bool operator==(const SlotWall&, const SlotWall&) = default;
};
static_assert(sizeof(SlotWall) == 32, "SlotWall: the 32 bytes its readers expect");

} // namespace terrain
