#pragma once
// Pure C++ layer of the Unreal plugin (no Unreal header): breaking and placing in game (phase 15,
// agent 28). From the point a trace of the camera hit on the tiles and the normal there, the edit
// of the tool (a block of 1 m, a sphere of 2 or 4 m across), the ghost shown before the click and
// the refusal of a placed volume inside the capsule of the pawn. Unreal traces, shows and queues;
// everything computed is here, tested by ctest.

#include "unreal_frame.hpp"

#include "terrain/storage/volume_edit.hpp"

#include <cstdint>

namespace terrain::unreal {

// The tools of the player.
enum class BuildTool : std::uint8_t {
    Block,   // one block of 1 m
    Sphere2, // a sphere 2 m across
    Sphere4, // a sphere 4 m across
};

// What a click does.
enum class BuildAction : std::uint8_t {
    Break, // removes matter (EditMode::Remove)
    Place, // adds rock (EditMode::Add)
};

// The name of a tool, for the screen and the log ("block 1 m", "sphere 2 m", "sphere 4 m").
[[nodiscard]] const char* toolName(BuildTool tool);
// The radius of a sphere tool, meters (0 for the block).
[[nodiscard]] double toolRadius(BuildTool tool);

// A direction of the core (x east, y up, z south), not necessarily normalized.
struct CoreDirection {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    friend bool operator==(const CoreDirection&, const CoreDirection&) = default;
};

// The direction of the core of an Unreal direction (X, Y, Z): the axes swapped as toCore, not
// scaled; and back.
[[nodiscard]] CoreDirection toCoreDirection(double x, double y, double z);
[[nodiscard]] UnrealPoint toUnrealDirection(const CoreDirection& direction);

// The axis of the largest component of a normal, as a unit step of blocks: the face of a block
// the normal looks out of. Equal components choose y, then x, then z (a fixed order). A null or
// non-finite normal gives the face up.
[[nodiscard]] BlockCoord dominantFace(const CoreDirection& normal);

// What a trace of the camera hit, in the core: the point on the surface, the normal of the
// surface there (out of the matter) and the distance from the eye, meters.
struct BuildHit {
    CorePoint point;
    CoreDirection normal;
    double distance = 0.0;
};

// The hit of a trace given in Unreal (centimeters, the frame of the session): the point and the
// distance converted by toCore, the normal by toCoreDirection.
[[nodiscard]] BuildHit hitFromUnreal(const UnrealFrame& frame, const UnrealPoint& point,
                                     double normalX, double normalY, double normalZ,
                                     double distanceCentimeters);

// The ghost of an edit: a box of the blocks (the cube of every block, edges on the half meters)
// or a sphere, in the core.
struct BuildGhost {
    bool sphere = false;
    CorePoint center;
    double halfX = 0.0; // box: half sizes, meters; sphere: the radius in all three
    double halfY = 0.0;
    double halfZ = 0.0;
};

// An edit aimed at: its volume and its ghost.
struct BuildTarget {
    VolumeEdit edit;
    BuildGhost ghost;
};

// How far the matter is looked into under the surface hit, meters: the block broken is the one
// whose center lies half a meter under the hit, along the normal, so that its center is in the
// matter and breaking it changes the surface (a block is the cube of side 1 m centered on a point
// of the lattice, modifications.hpp).
inline constexpr double kBreakDepthMeters = 0.5;

// The block of a point: the lattice point nearest to it (floor(v + 0.5) on each axis).
[[nodiscard]] BlockCoord blockOfPoint(const CorePoint& point);

// The edit of a tool for an action at a hit:
// - Break, block: the block under the hit (kBreakDepthMeters along -normal);
// - Place, block: its neighbour across the face the normal looks out of (dominantFace);
// - Break, sphere: centered on the block of the hit;
// - Place, sphere: centered half its radius out of the surface, along the normal.
// Only integers in the edit: the same hit gives the same edit on every machine.
[[nodiscard]] BuildTarget buildTarget(const BuildHit& hit, BuildTool tool, BuildAction action);

// True if a hit lies within reach of the eye (meters, the reach of the player, 6 m by default).
[[nodiscard]] bool withinReach(const BuildHit& hit, double reachMeters);

// The ghost in Unreal: its center (centimeters, the frame of the session) and its half sizes
// along Unreal X, Y and Z (centimeters).
struct UnrealGhost {
    bool sphere = false;
    UnrealPoint center;
    UnrealPoint half;
};
[[nodiscard]] UnrealGhost toUnrealGhost(const UnrealFrame& frame, const BuildGhost& ghost);

} // namespace terrain::unreal
