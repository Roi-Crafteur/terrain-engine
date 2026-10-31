#pragma once

#include "unreal_frame.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/storage/volume_edit.hpp"

#include <optional>
#include <span>
#include <string>

namespace terrain::unreal {

// Edits of the ground placed from Unreal, before the gameplay breaks and places blocks: the
// console command Terrain.Edit and ATerrainWorld::EditAhead, to see them from far away.
enum class GroundEdit {
    Raze,  // the ground of a square removed down under its lowest point
    Blast, // an explosion on the ground
    Heap,  // a heap of rock: a sphere added a third of its radius under the ground
};

// An edit of the ground ahead of the camera: its kind, its distance along the horizontal heading
// of the camera and its size, meters (the side of the square, the radius of the blast or heap),
// and for a razed square how deep under its lowest point it goes.
struct GroundEditCommand {
    GroundEdit kind = GroundEdit::Blast;
    double distance = 0.0;
    double size = 0.0;
    double depth = 10.0;
};

// The command of the arguments "raze <distance> <side> [depth]", "blast <distance> <radius>" or
// "heap <distance> <radius>" (meters), or nullopt with the reason in error: an unknown kind, a
// missing or non-positive number, a size beyond what one edit holds.
[[nodiscard]] std::optional<GroundEditCommand> parseGroundEdit(std::span<const std::string> args,
                                                               std::string& error);

// The core point distance meters ahead of the camera (an Unreal position) along the horizontal
// part of its forward vector (Unreal X and Y), its height left at 0.
[[nodiscard]] CorePoint pointAhead(const UnrealFrame& frame, const UnrealPoint& camera,
                                   double forwardX, double forwardY, double distance);

// The edit of a command around the core point (x, z), its heights read on the relief: a razed
// square goes from depth under its lowest ground (sampled every 8 m) to 10 m over its highest.
[[nodiscard]] VolumeEdit groundEditAt(const Heightfield& field, const GroundEditCommand& command,
                                      double x, double z);

} // namespace terrain::unreal
