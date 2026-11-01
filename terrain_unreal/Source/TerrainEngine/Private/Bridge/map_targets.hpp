#pragma once

// Pure C++ layer of the Unreal plugin (no Unreal header): compiled by Unreal with the plugin and
// by CMake for its tests (tests/unreal_map_*_test.cpp).
//
// Where the dev map sends the player (agent 35): the point clicked, the coordinates typed, the
// view saved from the camera. The teleport itself is ATerrainWorld::TeleportView (agent 36).

#include "map_places_port.hpp"
#include "map_view.hpp"
#include "place_pose.hpp" // yawOfHeading, headingOfYaw (agent 36)
#include "unreal_frame.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace terrain::unreal {

// The eye of a view the dev map sends to: this many meters above the ground or the water.
inline constexpr double kMapEyeAboveGround = 1.8;

// The view a click on a pixel of the map sends to: the point of the world under it, the heading
// and the pitch of the player kept, the eye kMapEyeAboveGround above the ground or the water.
[[nodiscard]] MapPlace clickTarget(const MapView& view, ScreenPoint pixel, double heading,
                                   double pitch, const WorldSize& world = {});

// The point typed in the box of the map: two numbers, x then z, separated by ";", or by "," when
// no ";" is there, or by spaces alone. Their digits may be grouped by spaces (also no-break and
// narrow no-break ones, as French writes numbers), the minus written "-" or "−", the decimals after
// a point (or after a comma when ";" separates the numbers): "-6 185 104 ; 155 924",
// "-241631, -86277", "3624471 -10006796". Nothing for anything else.
[[nodiscard]] std::optional<WorldPoint> parseCoordinates(std::string_view text);

// A number of meters as the map writes it: digits grouped by thousands with a narrow no-break
// space, "−" for the minus, decimals after a comma (French), such as "−6 185 104" or "1 725,26".
[[nodiscard]] std::string formatMeters(double meters, int decimals = 0);

// The view of a camera at a core point looking along an Unreal yaw and pitch (degrees): a place
// of that name at its canonical (x, z), its eye at its altitude.
[[nodiscard]] MapPlace placeFromCamera(std::string name, const CorePoint& eye, double yaw,
                                       double pitch, bool planet, std::string note,
                                       const WorldSize& world = {});

} // namespace terrain::unreal
