#pragma once
// Pure C++ layer of the Unreal plugin (no Unreal header): breaking and placing in game (phase 15,
// agent 28). From the point a trace of the camera hit on the tiles and the normal there, the edit
// of the tool (a block of 1 m, a sphere of 3 or 5 m across), the ghost shown before the click and
// the refusal of a placed volume inside the capsule of the pawn. Unreal traces, shows and queues;
// everything computed is here, tested by ctest.

#include "unreal_frame.hpp"

#include "terrain/storage/volume_edit.hpp"

#include <cstdint>
#include <string>

namespace terrain::unreal {

// The tools of the player.
enum class BuildTool : std::uint8_t {
    Block,   // one block of 1 m
    Sphere2, // a sphere 3.25 m across, 19 blocks (the small sphere; its name kept for Blueprints)
    Sphere4, // a sphere 4.9 m across, 57 blocks (the large sphere)
};

// What a click does.
enum class BuildAction : std::uint8_t {
    Break, // removes matter (EditMode::Remove)
    Place, // adds rock (EditMode::Add)
};

// The name of a tool, for the screen and the log ("block 1 m", "sphere 3 m", "sphere 5 m").
[[nodiscard]] const char* toolName(BuildTool tool);
// The name of a tool with the radius of its sphere (meters, ignored for the block): the name of
// the tool at its own radius, "sphere of radius 1.5 m" at another (Terrain.EditAim, agent 41).
[[nodiscard]] std::string shapeName(BuildTool tool, double radius);
// The radius of a sphere tool, meters (0 for the block): 26 / 16 m (19 blocks) and 39 / 16 m
// (57 blocks). A sphere centered on a block keeps the blocks within its radius (contains): at
// 1 m or 2 m, single blocks at the ends of its axes, which Marching Cubes draws as pyramids of
// 0.5 m (agent 41); at these radii every block but the center has two neighbors by a face in the
// sphere, and the hollow is round within 0.3 m (edit_hollow_test).
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
// The same with the radius of a sphere given (meters, rounded to the sixteenth of the edit; the
// block ignores it): the sphere of Terrain.EditAim (agent 41). buildTarget(hit, tool, action) is
// buildTarget(hit, tool, action, toolRadius(tool)).
[[nodiscard]] BuildTarget buildTarget(const BuildHit& hit, BuildTool tool, BuildAction action,
                                      double radius);

// True if a hit lies within reach of the eye (meters, the reach of the player, 6 m by default).
[[nodiscard]] bool withinReach(const BuildHit& hit, double reachMeters);

// A trace aimed through a point (Terrain.EditAim ... at x y z, agent 41) edits only where it
// hits within kAimToleranceMeters of that point: an obstacle on the way is refused, never edited.
inline constexpr double kAimToleranceMeters = 1.0;
[[nodiscard]] bool aimsAt(const BuildHit& hit, const CorePoint& point);

// The ghost in Unreal: its center (centimeters, the frame of the session) and its half sizes
// along Unreal X, Y and Z (centimeters).
struct UnrealGhost {
    bool sphere = false;
    UnrealPoint center;
    UnrealPoint half;
};
[[nodiscard]] UnrealGhost toUnrealGhost(const UnrealFrame& frame, const BuildGhost& ghost);

} // namespace terrain::unreal
