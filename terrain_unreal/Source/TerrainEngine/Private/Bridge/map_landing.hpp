#pragma once

// Pure C++ layer of the Unreal plugin (no Unreal header): compiled by Unreal with the plugin and
// by CMake for its tests (tests/unreal_map_targets_test.cpp).
//
// Where a click on the dev map (agent 35) puts the player: on land, the pawn of the game; over
// water with no dry ground near, the view of the tools above the water. It only decides between
// the two: the exact spot of the pawn and its checks are those of the tools of agent 36
// (ATerrainWorld::ReturnToGamePawn on Bridge/walkable_ground: candidates of the heightmap checked
// on the collision of the tiles, watched for a second).

#include "walkable_ground.hpp"

#include <optional>
#include <string>

namespace terrain {
class Heightfield;
}

namespace terrain::unreal {

enum class MapLandingKind {
    Land,      // the view goes to (x, z), then ReturnToGamePawn gives the pawn of the game back
    OverWater, // the view of the tools stays above the water at the click
};

struct MapLanding {
    MapLandingKind kind = MapLandingKind::Land;
    double x = 0.0; // where the view of the tools goes: the click, or the dry column nearest to it
    double z = 0.0;
    double clickX = 0.0;
    double clickZ = 0.0;
    bool clickWet = false; // the column clicked holds water (columnHasWater)
    WalkableReason reason = WalkableReason::Found;
};

// The decision for a click at (x, z), the dry ground looked for within radius meters as
// Terrain.Pawn looks for it (walkableCandidates, its rings, any slope: the walkable spot is the
// choice of ReturnToGamePawn):
// - a dry column clicked: Land there;
// - a wet one with dry ground in the radius (a shallow shore): Land at the nearest dry column
//   (ReturnToGamePawn refuses a view over water, OverWater);
// - water everywhere in the radius (NoDryGround: the open sea, the middle of a lake): OverWater.
[[nodiscard]] MapLanding clickLanding(double x, double z, double radius,
                                      const GroundHeight& height, const GroundWet& wet);
// clickLanding over the ground of the core: its heightmap, and its water (columnHasWater).
[[nodiscard]] MapLanding clickLandingOn(const Heightfield& field, double x, double z,
                                        double radius);

// Where the view goes once ReturnToGamePawn refused the dry column of a Land (none of its spots
// holds the pawn: a cliff at the edge of a lake): back above the water at the click for a wet
// click, never left against the wall; nothing for a dry click (the view of the tools stays).
[[nodiscard]] std::optional<MapLanding> landingFallback(const MapLanding& landing);

// What the map says of a landing, for the screen and the log (English, as the tools).
[[nodiscard]] std::string landingText(const MapLanding& landing, double radius);

} // namespace terrain::unreal
