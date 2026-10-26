#pragma once

// The valley of a course at a point (phase 11B), internal to terrain_core: the surface carved
// across it, from its axis outward. The channel (a parabola depth deep, its banks rising at
// kBankSlope, cut into the floor) meanders about the axis in the plains; the floor of the valley
// (a floodplain, the floor of a U, nothing in a gorge) rises gently from just above the water; then
// the walls, rounded at their foot. The surface of a course is the lowest over its segments (the
// point nearest to each, its attributes interpolated there): continuous everywhere, smooth along
// the course, never a step. Pure arithmetic of doubles and hashes: the same bits on every
// platform.

#include "river_course.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace terrain::water {

// The valley of a course at a point.
struct Valley {
    double height = std::numeric_limits<double>::infinity(); // the carved surface
    float level = 0.0f;   // the water surface of the course there
    float floorLevel = 0.0f; // the level its floor is carved from (CoursePoint::valley), rising
                             // upstream of the segment as its surface
    bool wet = false;     // in its channel: under its water
    float gorge = 0.0f;
    float mountain = 0.0f;
    double upX = 1.0;     // unit direction away from the axis
    double upZ = 0.0;
    double slope = 0.0;   // of the floor and walls across the valley (meters per meter)
    double above = 0.0;   // of the walls above the floor (meters): where the rock shows
    // The floor of the valley (its floodplain) and how much the point lies on it: 1 on the floor,
    // fading to 0 kLeveeFade meters up the walls.
    double floor = -std::numeric_limits<double>::infinity();
    double onFloor = 0.0;
    // valleyAt: the most the floor of the course fills the relief at the point (kLevee at most).
    double raise = 0.0;
    // The banks of its channel: the ground beside them is held up to this height, kFloorRise above
    // the water for kBankFlat meters from the edge, then down kBankDrop meters per meter to
    // kBankReach meters (-infinity in the channel and beyond): no valley of another river, nor of
    // a stretch below on the same one, leaves the water standing above the ground beside it.
    double bank = -std::numeric_limits<double>::infinity();
};

inline constexpr double kBankFlat = 8.0;
inline constexpr double kBankDrop = 0.5;
inline constexpr double kBankReach = 60.0;

// The rock of the walls of a valley follows its direction, slopes, walls, gorge and mountains:
// blended over the valleys near the lowest (a weight halving every kBlendHalving meters above
// it, none from kBlend above), they never jump where the lowest one changes (a bend of a course,
// the inside of a loop, the joint of two reaches).
inline constexpr double kBlend = 2.0;
inline constexpr double kBlendHalving = 0.25;

class ValleyBlend {
public:
    void add(const Valley& v);
    // Sets the blended attributes on v (the lowest), when the blend holds any.
    void applyTo(Valley& v) const;

private:
    double lowest_ = std::numeric_limits<double>::infinity();
    double weight_ = 0.0;
    double upX_ = 0.0;
    double upZ_ = 0.0;
    double slope_ = 0.0;
    double above_ = 0.0;
    double gorge_ = 0.0;
    double mountain_ = 0.0;
};

// The valley of course c at (x, z), in the images of c, where the relief is ground meters high:
// its surface wherever it lies below ground + kLevee (otherwise height stays infinite), and the
// fill of its floor there (raise). Its rock attributes are blended over its segments
// (ValleyBlend). meanderSeed draws the meanders; warp in [-1, 1] (a noise at the point) moves the
// walls by up to wallWarpOf of the floor.
[[nodiscard]] Valley valleyAt(const Course& c, double x, double z, double ground,
                              std::uint64_t meanderSeed, double warp);

// The rise of the surface of each segment of a course beyond its ends (meters per meter): the
// fastest the level (CoursePoint::level, the water, or CoursePoint::valley) upstream of its start
// comes down to it, or the level downstream of its end comes down from it, kLeastRise at least.
[[nodiscard]] std::vector<float> risesOf(const std::vector<CoursePoint>& points,
                                         float CoursePoint::*level);

// The offset of the channel from the axis (meters, toward the left of the course) at phase, for
// an amplitude, and its rate per half wave: a gradient noise of one half wave per unit.
struct Meander {
    double offset = 0.0;
    double rate = 0.0;
};
[[nodiscard]] Meander meanderAt(double phase, double amplitude, std::uint64_t seed);

} // namespace terrain::water
