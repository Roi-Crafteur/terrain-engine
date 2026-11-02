#pragma once
// Pure C++ layer of the Unreal plugin (no Unreal header): the rules of breaking and placing in
// game (agent 28), next to block_targeting.hpp. When a click may edit at all (BuildGate), how
// often a held button edits (EditRate), and the refusal of a placed volume that would close on the
// capsule of the pawn (CoreCapsule, touchesCapsule).

#include "unreal_frame.hpp"

#include "terrain/storage/volume_edit.hpp"

namespace terrain::unreal {

// What Unreal knows at a click.
struct BuildGate {
    bool playing = false;          // the world streams (a session exists)
    bool devMapOpen = false;       // the dev map takes the clicks and the wheel
    bool loading = false;          // a teleport holds the view: the screen black or the pawn held
    bool controlsOwner = false;    // the player controls the pawn of the component (not the
                                   // flying pawn of the tools)
    bool focused = false;          // the window of the game has the focus
};

// Why a click does nothing, or nullptr when it may edit.
[[nodiscard]] const char* gateRefusal(const BuildGate& gate);

// Edits at most every interval seconds while a button is held: the first press edits at once,
// a new press after a release too (a fast clicker is limited by the same interval).
class EditRate {
public:
    explicit EditRate(double intervalSeconds = 0.25) : interval_(intervalSeconds) {}

    // True if an edit may go now (it is then counted): the button pressed or held at now.
    [[nodiscard]] bool allow(double nowSeconds);
    [[nodiscard]] double interval() const { return interval_; }

private:
    double interval_;
    double last_ = 0.0;
    bool any_ = false;
};

// The capsule of a pawn in the core: the vertical axis at (x, z), its feet at bottom and its top
// at top (meters, the core altitude), its radius.
struct CoreCapsule {
    double x = 0.0;
    double z = 0.0;
    double bottom = 0.0;
    double top = 0.0;
    double radius = 0.0;
};

// The capsule of an Unreal capsule component: its center (centimeters, the frame of the session),
// its scaled half height (from the center to the top of the cap) and radius, centimeters.
[[nodiscard]] CoreCapsule capsuleFromUnreal(const UnrealFrame& frame, const UnrealPoint& center,
                                            double halfHeightCentimeters,
                                            double radiusCentimeters);

// A margin kept between a placed volume and the capsule, meters: the meshing smooths a placed
// block over its neighbours, never into the pawn.
inline constexpr double kCapsuleMarginMeters = 0.05;

// Distance from the capsule (its surface) to the cubes of every block an edit may change
// (editBounds, the cube of a block from its center - 0.5 to + 0.5), meters; negative inside.
// The edit and the capsule in the same image of the world.
[[nodiscard]] double capsuleDistance(const VolumeEdit& edit, const CoreCapsule& capsule);

// True if a placed volume would come within kCapsuleMarginMeters of the capsule: refused.
[[nodiscard]] bool touchesCapsule(const VolumeEdit& edit, const CoreCapsule& capsule);

} // namespace terrain::unreal
