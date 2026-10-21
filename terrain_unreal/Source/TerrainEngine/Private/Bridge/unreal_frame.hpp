#pragma once

// Pure C++ layer of the Unreal plugin (no Unreal header): compiled by Unreal with the plugin
// and by CMake for its tests (tests/unreal_*_test.cpp).

#include "terrain/chunk_coord.hpp"
#include "terrain/meshing/mesh.hpp"
#include "terrain/world_size.hpp"

namespace terrain::unreal {

// Unreal works in centimeters, terrain_core in meters.
inline constexpr double kCentimetersPerMeter = 100.0;

// A point of the terrain_core world: meters, Y up (right-handed: x east, z south).
struct CorePoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend bool operator==(const CorePoint&, const CorePoint&) = default;
};

// A point of the Unreal world: centimeters, Z up (left-handed: X forward, Y right). Doubles,
// as the FVector of the Large World Coordinates.
struct UnrealPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend bool operator==(const UnrealPoint&, const UnrealPoint&) = default;
};

// A vector of an Unreal mesh: centimeters relative to the origin of its component, or a unit
// direction.
struct UnrealVector3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend bool operator==(const UnrealVector3f&, const UnrealVector3f&) = default;
};

// Where the terrain_core world lies in the Unreal world: origin is the core point placed at the
// Unreal origin. The only conversion between the two worlds:
//   X = 100 (x - origin.x), Y = 100 (z - origin.z), Z = 100 (y - origin.y).
// Swapping Y and Z is the exchange of axes that does not mirror the world between the
// right-handed core and the left-handed Unreal: facing east (x, X), south (z, Y) lies on the
// right in both. As a map of coordinates it is a reflection (determinant -1): it reverses
// cross products, which keeps the index order of the triangles (tile_mesh_conversion.hpp).
//
// Extension point of the origin rebasing: shifted() follows Unreal when it moves the world.
//
// The core world wraps around (WorldSize), the Unreal world does not: the player keeps unwrapped
// core coordinates across the edges, and every tile is shown at its image nearest to it
// (tileOrigin, TileStreamer), so crossing an edge changes nothing in the frame. Far from the
// origin, the player is moved back by whole worlds (worldTurnOffset), the frame unchanged.
struct UnrealFrame {
    CorePoint origin;

    friend bool operator==(const UnrealFrame&, const UnrealFrame&) = default;
};

[[nodiscard]] UnrealPoint toUnreal(const UnrealFrame& frame, const CorePoint& point);
[[nodiscard]] CorePoint toCore(const UnrealFrame& frame, const UnrealPoint& point);

// Offset of a mesh vertex from the mesh origin, meters to centimeters (no frame involved).
[[nodiscard]] UnrealVector3f toUnrealOffset(const Vec3f& meters);

// A direction such as a normal: the axes swapped, not scaled.
[[nodiscard]] UnrealVector3f toUnrealDirection(const Vec3f& direction);

// The frame after Unreal moved every actor of the world by offset (centimeters, as
// AActor::ApplyWorldOffset): every core point maps to its former Unreal position + offset.
[[nodiscard]] UnrealFrame shifted(const UnrealFrame& frame, const UnrealPoint& offset);

// Chunk of the core world under an Unreal position.
[[nodiscard]] ChunkCoord chunkAt(const UnrealFrame& frame, const UnrealPoint& point);

// Beyond this fraction of a world period from the Unreal origin, along X (core x) or Y (core z),
// the player is moved back by whole periods (worldTurnOffset). Above half a world, so that
// the player lands within a quarter of a world past the opposite threshold and never moves
// back and forth; below one world, so that Unreal coordinates stay under 0.75 of the largest
// side (about 45,000 km, well within the 88,000 km of the Large World Coordinates).
inline constexpr double kWorldTurnThreshold = 0.75;

// The offset (centimeters, whole world periods along X and Y, 0 along Z) to add to an Unreal
// position further than kWorldTurnThreshold of a period from the Unreal origin: the core place
// under it is the same (the world wraps around), and after the move the position lies within
// kWorldTurnThreshold of a period of the origin again. Zero closer to the origin. Adding it is
// exact: a whole number of periods is an integer number of centimeters, smaller results stay
// on the grid of the position. The tiles follow the player to their nearest images by the same
// periods (TileStreamer: TileMove), so nothing moves relative to the player.
[[nodiscard]] UnrealPoint worldTurnOffset(const UnrealPoint& position, const WorldSize& world);

} // namespace terrain::unreal
