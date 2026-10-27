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
#include "terrain/det_math.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <algorithm>
#include <cmath>
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

// The noise of the walls at a point (the warp of valleyAt, in [-1, 1]): given, or drawn the first
// time a segment needs it (at the canonical point, through a memo or not: the same bits).
class WallWarp {
public:
    explicit WallWarp(double value) : value_(value), known_(true) {}
    WallWarp(const noise::FbmOctaves& noise, double x, double z, noise::FbmOctaves::Memo* memo)
        : noise_(&noise), memo_(memo), x_(x), z_(z) {}

    [[nodiscard]] double value() {
        if (!known_) {
            value_ = static_cast<double>(memo_ != nullptr ? noise_->fbm2D(x_, z_, *memo_)
                                                          : noise_->fbm2D(x_, z_));
            known_ = true;
        }
        return value_;
    }

private:
    const noise::FbmOctaves* noise_ = nullptr;
    noise::FbmOctaves::Memo* memo_ = nullptr;
    double x_ = 0.0;
    double z_ = 0.0;
    double value_ = 0.0;
    bool known_ = false;
};

// The distance from (x, z) to the segment a-b.
[[nodiscard]] inline double distanceToSegment(double x, double z, double ax, double az, double bx,
                                              double bz) {
    const double dx = bx - ax;
    const double dz = bz - az;
    const double length2 = dx * dx + dz * dz;
    const double t =
        length2 > 0.0 ? std::clamp(((x - ax) * dx + (z - az) * dz) / length2, 0.0, 1.0) : 0.0;
    return dmath::hypot(x - (ax + t * dx), z - (az + t * dz));
}

// The rise of the surface of a segment beyond its ends (risesOf): kLeastRise at least.
inline constexpr double kLeastRise = 2.0;
// The run of segment a-b: across, along, and its length squared and its length.
struct SegmentSpan {
    SegmentSpan() = default;
    SegmentSpan(const CoursePoint& a, const CoursePoint& b)
        : dx(b.x - a.x), dz(b.z - a.z), length2(dx * dx + dz * dz),
          length(length2 > 0.0 ? std::sqrt(length2) : 0.0) {}
    double dx = 0.0;
    double dz = 0.0;
    double length2 = 0.0;
    double length = 0.0;
};

// The floor of the valley lies kFloorRise meters above the water at the edge of the channel.
inline constexpr double kFloorRise = 0.6;
// The walls stand at least kWallMargin meters beyond the channel, bounded softly over kWallSoft.
inline constexpr double kWallMargin = 2.0;
inline constexpr double kWallSoft = 6.0;
// The slope of the walls of a gorge swings by up to kGorgeSwing of itself along it.
inline constexpr double kGorgeSwing = 0.45;
// The channel meanders at most kMeanderReach times its amplitude from the axis, its offset
// changing at most kMeanderRate times its amplitude per half wave (meanderAt: gradients within
// [-1, 1], 2 (1 + 1.875 + 2) for the rate), both with a margin.
inline constexpr double kMeanderReach = 2.001;
inline constexpr double kMeanderRate = 9.76;

// What of a course may change its valley at the points of a box (Hydrology::Region), in the
// images of the course: its chunks that some point of the box may not skip, each with the least
// away and floor of valleyAt over the box (where away is positive and floor lies kBlend above
// the lowest valley found so far, the point skips it), and of their segments those that may
// change the valley at some point of the box. Every other chunk or segment changes nothing there.
struct BoxCourse {
    struct Chunk {
        std::uint32_t chunk = 0; // in Course::chunks
        double away = 0.0;
        double floor = 0.0;
        std::uint32_t first = 0; // its segments: segments[first, last)
        std::uint32_t last = 0;
    };
    // A segment, with what bounds its valley at any point (river_box.cpp): the widest channel,
    // meanders and floor, the gentlest walls and the most gorge, the longest foot, the lowest its
    // surface starts from and the highest its floor and its slope, the narrowest channel; its
    // banks within bankFrom of its axis (the meanders at most meander away, the channel stretched
    // by stretch at most). keep: of a shape these bounds do not hold for, always kept.
    struct Segment {
        std::uint32_t first = 0; // index of its first point
        SegmentSpan span;
        bool keep = false;
        double least = 0.0;     // the walls never nearer to the axis (channel, meanders, margin)
        double floorHalf = 0.0;
        double wall = 0.0;
        double gorge = 0.0;
        double foot = 1.0;
        double low = 0.0;
        double lowStart = 0.0; // the lowest its surface starts from at its start, at its end
        double lowEnd = 0.0;
        double floorTop = 0.0;
        double floorSlope = 0.0;
        double half = 0.0;
        double bankFrom = 0.0;
        double meander = 0.0;
        double stretch = 1.0;
        // Whether its valley may change valleyAt at a point d meters from it, beyond meters
        // beyond its ends (upstream of its start or not), where the relief lies at least
        // groundLow high, the noise of the walls warp there (or below, as -1 bounds every point
        // of a box), and the valley joins the blend below threshold.
        [[nodiscard]] bool mayMatter(double d, double beyond, bool upstream, double groundLow,
                                     double threshold, double warp) const;
    };
    std::vector<Chunk> chunks;
    std::vector<Segment> segments;
};
// The box [minX, maxX] x [minZ, maxZ] of points of course c (its images) where the relief stands
// between groundMin and groundMax meters high (river_box.cpp).
[[nodiscard]] BoxCourse boxCourse(const Course& c, double minX, double minZ, double maxX,
                                  double maxZ, double groundMin, double groundMax);

// The valley of course c at (x, z), in the images of c, where the relief is ground meters high:
// its surface wherever it lies below ground + kLevee (otherwise height stays infinite), and the
// fill of its floor there (raise). Its rock attributes are blended over its segments
// (ValleyBlend). meanderSeed draws the meanders; warp in [-1, 1] (a noise at the point) moves the
// walls by up to wallWarpOf of the floor. box: what of c may change the valley in a box holding
// the point and its ground (boxCourse), or null. A segment is only evaluated where it may change
// the result: the same bits whatever is left out.
[[nodiscard]] Valley valleyAt(const Course& c, double x, double z, double ground,
                              std::uint64_t meanderSeed, WallWarp& warp,
                              const BoxCourse* box = nullptr);
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
