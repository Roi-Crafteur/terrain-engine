#pragma once

// One segment of a course at a point (river_profile.cpp), internal to terrain_core: the foot of the
// point on it, the frame of its valley there, what of it may matter and its valley; for the
// search over the segments of valleyAt (river_search.cpp).

#include "river_profile.hpp"

#include <cstddef>
#include <cstdint>

namespace terrain::water {

// The point of segment a-b nearest to (x, z): the share t along it, the distance and how far the
// point lies beyond its ends along it (0 alongside), upstream of its start or downstream of its
// end; then (sideOf) the unit direction from it to the point and the signed distance to the line
// of the segment (left of the course positive).
struct Foot {
    double t = 0.0;
    double distance = 0.0;
    double qx = 0.0; // from it to the point
    double qz = 0.0;
    double beyond = 0.0;
    bool upstream = false;
    double side = 0.0;
    double upX = 1.0;
    double upZ = 0.0;
};

// The foot of (x, z) on segment a-b (its first point a).
[[nodiscard]] Foot footOn(const CoursePoint& a, const SegmentSpan& s, double x, double z);

// How far (x, z) lies left of the line of segment a-b (positive) or right of it.
[[nodiscard]] double sideAt(const CoursePoint& a, const SegmentSpan& s, double x, double z);

// Where the foot of a point lies along the course (beyond the ends of segment a-b too).
[[nodiscard]] double alongOf(const CoursePoint& a, const CoursePoint& b, const Foot& f);

// What bounds the surface of the valley of segment a-b at a foot: the water and the levels there,
// the walls and the floor (segmentValley goes on from them to the channel).
struct Frame {
    double t = 0.0;
    double level = 0.0;
    double ground = 0.0;
    double water = 0.0;
    double surfaceLevel = 0.0;
    double fillLevel = 0.0;
    double half = 0.0;
    double depth = 0.0;
    double amplitude = 0.0;
    double out = 0.0;
    double walls = 0.0;
    double wallSlope = 0.0;
    double floorSlope = 0.0;
    double above = 0.0;
    double floorLevel = 0.0;
    // On a stretch in steps (fall above 0), the walls of its slot (CoursePoint::slot), and the
    // share its channel widens by in a bowl (river_slot.hpp).
    double fall = 0.0;
    double slot = 0.0;
    double channel = 0.0; // the half width of its channel, narrowed in a slot (slotHalf)
    double wider = 0.0;
    double along = 0.0; // where the foot lies along the course
};

// The frame of segment i of course c at the foot f of (x, z) on it: in the frame of the slot of a
// corridor (river_winding.hpp), with the bowl of a pool below a drop (river_slot.hpp).
[[nodiscard]] Frame frameAt(const Course& c, std::size_t i, const SegmentSpan& span, double x,
                            double z, Foot& f, double warp);

// What of the valley of segment a-b at a foot may change valleyAt over relief ground meters high:
// its surface below threshold (the lowest it may join the blend) or its floor filling the relief
// (all of it), else its banks there (only them), else nothing. Exact: the surface is never below
// the floor and the walls cut down to the water less the depth of the channel (the channel is
// never lower), the fill is that of segmentValley, and the banks only stand within kBankReach of
// the channel.
[[nodiscard]] Matters mayMatter(const CoursePoint& a, const CoursePoint& b, const Foot& f,
                                const Frame& r, double threshold, double ground);

// The channel of segment a-b at a foot (channelOf).
struct Channel {
    double distance = 0.0;
    double half = 0.0;
    double height = 0.0;
    double bowl = 0.0; // what its bowls widen it by
};

// The channel of segment a-b at a foot: how far the point lies from its meandering line, its
// half width and the height of its bed or of its banks there. On a stretch in steps, in the slot of
// the fall (river_slot.hpp): the channel narrowed, its walls rising from the water to the floor
// over their own width beyond it, whatever the depth of the steps below the floor.
// Where the channel of segment a-b lies from a foot (channelOf): its distance and its half width.
[[nodiscard]] Channel channelReach(const CoursePoint& a, const CoursePoint& b, const Foot& f,
                                   const Frame& r, std::uint64_t seed);

// The banks of a channel ch of the frame r (Valley::bank): within kBankReach of its edge.
[[nodiscard]] double bankOf(const Channel& ch, const Frame& r);

// The rest of the valley of segment a-b at a foot from its frame. Upstream of its start, the
// surface rises rise meters per meter (risesOf: at least as fast as the level upstream of it
// comes down to it): on a steep stretch or below a fall the floor of the segments below never
// undercuts the water above them, while the segments alongside the point set its surface. The
// fill of the floor drops as fast downstream of its end, for the same reason. The valley is carved
// from its level (CoursePoint::valley, its surface rising valleyRise), the channel from the water
// (rise): on a fall, in its slot cut into the floor of the valley (channelOf).
[[nodiscard]] Valley segmentValley(const CoursePoint& a, const CoursePoint& b, const Foot& f,
                                   const Frame& r, std::uint64_t seed, std::uint64_t slotSeed);

} // namespace terrain::water
