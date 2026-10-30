#pragma once

#include "unreal_frame.hpp"

#include "terrain/streaming/stream_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain::unreal {

// A UV of an Unreal mesh (two floats; the procedural meshes keep them as half floats).
struct UnrealUV {
    float u = 0.0f;
    float v = 0.0f;

    friend bool operator==(const UnrealUV&, const UnrealUV&) = default;
};

// What a vertex of the water of a tile is (UV channel 1, u): the surface of a river, of a lake or
// of a salt lake (buildTileWater), the curtain of a fall or the foam at its foot
// (buildFallCurtains). The water materials read it to color the water by its place.
inline constexpr float kWaterRiver = 1.0f;
inline constexpr float kWaterLake = 2.0f;
inline constexpr float kWaterSaltLake = 3.0f;
inline constexpr float kWaterCurtain = 4.0f;
inline constexpr float kWaterFoam = 5.0f;

// The water of a tile as Unreal takes it (agent 27), a mesh of its own beside the ground: positions
// in centimeters relative to the origin of the tile (as UnrealMesh), Z up, already at the level
// of the water (no world position offset: the bounds hold it); unit normals toward the air;
// three indices per triangle, front faces up (or downstream on a curtain), as Unreal expects
// them.
// - uv0: on the surface, the direction downstream of the river (Unreal X and Y, a unit vector; 0
//   on a lake); on a curtain or the foam, meters across and down the fall from its lip.
// - uv1: (what the vertex is, kWater*; on the surface the slope of the water, meters per meter,
//   on a curtain or the foam the height of the fall, meters).
// - uv2: (the altitude of the water, meters, 0).
struct UnrealWaterMesh {
    std::vector<UnrealVector3f> positions;
    std::vector<UnrealVector3f> normals;
    std::vector<UnrealUV> uv0;
    std::vector<UnrealUV> uv1;
    std::vector<UnrealUV> uv2;
    std::vector<std::int32_t> triangles;

    [[nodiscard]] std::size_t triangleCount() const { return triangles.size() / 3; }
    [[nodiscard]] bool empty() const { return triangles.empty(); }

    friend bool operator==(const UnrealWaterMesh&, const UnrealWaterMesh&) = default;
};

// Above this slope (meters per meter) between two corners of a cell, the water of a tile is cut:
// the face of a fall, which its curtain draws (buildFallCurtains).
inline constexpr double kWaterCutSlope = 1.5;

// The surface of the lakes and rivers of a tile (TileData::waterColumns; empty without them), a
// grid of cells on its columns:
// - a cell is drawn where a corner lies under a river, a lake or a salt lake; the sea is left to
//   the horizon, which draws it whole;
// - a wet corner stands at the level of its column (HeightSample::waterLevel, bit for bit in
//   meters) and is shared by its cells; a dry corner of a drawn cell stands at the mean level of
//   the wet corners of that cell (its own vertex): the water goes on under the bank, whose ground
//   hides it;
// - a cell steeper than kWaterCutSlope between two corners is cut;
// - on a side stitched to a coarser tile (TileRequest::seams), an odd column between two wet
//   even ones stands at their mean level: on the straight edge of the coarser cell, no crack
//   between the LODs. Two tiles of a LOD share the columns of their common side: the same
//   vertices there, bit for bit.
// Deterministic: a pure function of the data of the tile.
[[nodiscard]] UnrealWaterMesh buildTileWater(const TileData& data);

} // namespace terrain::unreal
