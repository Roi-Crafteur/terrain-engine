#pragma once

#include "unreal_frame.hpp"

#include "terrain/dev/places_file.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/world_size.hpp"

#include <string>

namespace terrain::unreal {

// Where a view stands and where it looks (the tools of the developers, agent 36: Terrain.Goto,
// Terrain.Look, Terrain.Eye, Terrain.Save and ATerrainWorld::TeleportView): the eye, a point of
// the core in meters, and the yaw and the pitch of the camera of Unreal, in degrees.
struct ViewPose {
    CorePoint eye;
    double yaw = 0.0;   // Unreal: 0 toward +X (east, core x), 90 toward +Y (south, core z)
    double pitch = 0.0; // up, in [-90, 90]

    friend bool operator==(const ViewPose&, const ViewPose&) = default;
};

// The rotation of the camera of Unreal, degrees, in the order of FRotator(Pitch, Yaw, Roll).
// The pitch is positive up and negative down everywhere (places, Terrain.Look, TeleportView,
// FRotator): -45 looks 45 degrees below the horizon.
struct UnrealRotation {
    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;

    friend bool operator==(const UnrealRotation&, const UnrealRotation&) = default;
};

// A unit vector in the axes of Unreal (X east, Y south, Z up).
struct UnrealDirection {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// The heading of the places (dev::Place): degrees clockwise from the north, the -z of the core
// and the -Y of Unreal. The yaw of Unreal is heading - 90, brought into (-180, 180]; the heading
// of a yaw is yaw + 90, brought into [0, 360).
[[nodiscard]] double yawOfHeading(double heading);
[[nodiscard]] double headingOfYaw(double yaw);

// The top of a column: the level of its water when it holds some (the sea, a lake, a river),
// otherwise its ground.
[[nodiscard]] double columnTop(const HeightSample& sample);

// The column holds water over its ground (the sea, a lake, a river, a salt lake): no pawn of the
// game is put down there (ATerrainWorld::ReturnToGamePawn).
[[nodiscard]] bool columnHasWater(const HeightSample& sample);

// The altitude of an eye: eye itself (EyeMode::Altitude, an altitude of the core), or eye meters
// above top, the top of its column (EyeMode::Ground).
[[nodiscard]] double eyeAltitude(double eye, dev::EyeMode mode, double top);

// The pose of a view from (x, z), its column topped at top: the eye at eyeAltitude, the yaw of
// heading, the pitch clamped to [-90, 90].
[[nodiscard]] ViewPose viewPose(double x, double z, double heading, double pitch, double eye,
                                dev::EyeMode mode, double top);
[[nodiscard]] ViewPose placePose(const dev::Place& place, double top);

// The rotation of the camera for a pose (TeleportView), or for a heading and a pitch
// (Terrain.Look): the yaw of the pose (or of heading), its pitch clamped to [-90, 90] and kept
// as it is (negative: down), no roll.
[[nodiscard]] UnrealRotation viewRotation(const ViewPose& pose);
[[nodiscard]] UnrealRotation viewRotation(double heading, double pitch);

// Where a rotation looks, as FRotator::Vector gives it: (cos pitch cos yaw, cos pitch sin yaw,
// sin pitch). Its Z is negative for a pitch down; heading 0 looks along -Y (the north).
[[nodiscard]] UnrealDirection lookDirection(const UnrealRotation& rotation);

// The pose of a camera of Unreal (GetCameraRotation): Unreal keeps its pitch in [0, 360) (315 is
// 45 degrees down): pitch and yaw brought into (-180, 180], the pitch then clamped to [-90, 90].
[[nodiscard]] ViewPose cameraPose(const CorePoint& eye, double pitch, double yaw);

// The place of a pose (Terrain.Save): (x, z) brought into the canonical range of world, the eye
// as an altitude; positions and eye rounded to the centimeter, heading and pitch to the
// hundredth of a degree (the heading in [0, 360)). Throws dev::PlacesError for an invalid name.
[[nodiscard]] dev::Place placeOfView(const std::string& name, const std::string& note,
                                     const ViewPose& pose, bool planet, const WorldSize& world);

} // namespace terrain::unreal
