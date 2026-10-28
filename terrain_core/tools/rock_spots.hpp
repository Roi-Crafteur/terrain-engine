#pragma once

// The spots of terrain_export_rock (export_rock.cpp) to check the rock of phase 12A in a client:
// the tallest face near a point seen from in front under a low sun from its side (its grooves
// stand out), and a spot from which to walk straight to a face, or under an overhang (the
// collision of the tiles).

#include "rock_places.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include <string>

namespace rock_tool {

// The tallest face within 150 m of (x, z) at its full weight, seen from in front 1.6 times its
// height away, level with its middle, under a sun from its left 20 degrees high: shot
// "face_side". False without one.
bool addFaceSide(const terrain::Heightfield& field, double x, double z, Places& out);

// A spot of flat ground (slopes under 10 degrees 3 m around it) from which a walker goes straight
// 15 to 40 m over walkable ground (under 25 degrees per meter) to the foot of a vertical face at
// least 8 m high (overhang false), or under the lip of a cap at least 2 m deep with 2.5 m of air
// under it at least (overhang true), within 300 m of (x, z).
struct WalkSpot {
    bool found = false;
    double x = 0.0;
    double z = 0.0;
    float ground = 0.0f;   // the altitude of the ground there, meters (TerrainTiles::groundAt)
    double heading = 0.0;  // degrees clockwise from the north (-Z), toward the face
    double distance = 0.0; // to the face, meters
    double faceX = 0.0;    // the middle of the face
    double faceZ = 0.0;
    float foot = 0.0f;
    float top = 0.0f;
    double cap = 0.0;
    double ceiling = 0.0; // overhang: the height of the air under the lip, meters
};
[[nodiscard]] WalkSpot walkToFace(const terrain::TerrainTiles& tiles, double x, double z,
                                  bool overhang);

// The view of a walker at a spot, toward its face (name), and its description.
[[nodiscard]] Shot walkShot(const WalkSpot& spot, const std::string& name);
[[nodiscard]] std::string describe(const WalkSpot& spot, bool overhang);

} // namespace rock_tool
