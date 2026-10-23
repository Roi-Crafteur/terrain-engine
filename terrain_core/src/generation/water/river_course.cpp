// The course of a reach: its axis, its water level and the shape of its valley along it
// (river_course.hpp).
#include "river_course.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain::water {

namespace {

// A tributary bends toward the point where it meets the river it joins over its last kJoinFade
// meters; the meanders fade out over kMeanderTaper meters at the confluences, lakes and mouths.
constexpr double kJoinFade = 1'500.0;
constexpr double kMeanderTaper = 1'500.0;
// Where the bed must come down to the end of the reach below the relief (a sill the planet level
// cut), it comes down at least kRampSlope meters per meter.
constexpr double kRampSlope = 0.02;
// A fall: the water drops more than kFallDrop meters at more than kFallSlope between two points;
// the stretch is then sampled every kFallStep meters, so that the lip is sharp.
constexpr float kFallDrop = 4.0f;
constexpr double kFallSlope = 0.12;
constexpr double kFallStep = 8.0;
// The mountains: the mask from 0 to kMountainFull.
constexpr float kMountainFull = 0.25f;
// The deepest the river cuts below the relief: kPlainCut in the plains, kMountainCut under the
// mountains (PlanetPassParams; deeper, the planet level holds a lake behind the sill).
constexpr double kPlainCut = 100.0;
constexpr double kMountainCut = 300.0;
// The gorge: as the relief above the water on the axis passes kGorgeLow to kGorgeHigh meters
// (under the mountains) or kCanyonLow to kCanyonHigh (a canyon in a plateau of the plains).
constexpr float kGorgeLow = 60.0f;
constexpr float kGorgeHigh = 200.0f;
constexpr float kCanyonLow = 25.0f;
constexpr float kCanyonHigh = 80.0f;
// The valleys. Plains: a floodplain beyond the belt of the meanders by kPlainMargin widths, rising
// kPlainFloorSlope across, then banks of kPlainWall rounded over kPlainFoot meters. Mountains: a U,
// its floor kUFloorBase + kUFloorGrowth log10(Q / 10) meters on each side, rising kUFloorSlope,
// walls of kUWall rounded over kUFoot. Gorges: a floor kGorgeFloor meters wider than the channel,
// walls of kGorgeWall (kCanyonWall in the plains) rounded over kGorgeFoot.
constexpr double kPlainMargin = 2.0;
constexpr double kPlainFloorSlope = 0.004;
constexpr double kPlainWall = 0.15;
constexpr double kPlainFoot = 60.0;
constexpr double kUFloorBase = 120.0;
constexpr double kUFloorGrowth = 90.0;
constexpr double kUFloorSlope = 0.03;
constexpr double kUWall = 0.7;
constexpr double kUFoot = 150.0;
constexpr double kGorgeFloor = 4.0;
constexpr double kGorgeWall = 3.0;
constexpr double kCanyonWall = 2.5;
constexpr double kGorgeFoot = 8.0;
// The meanders: an amplitude of kMeanderAmplitude widths and half waves of kMeanderHalfWave
// widths (a wavelength of about 11 widths), on slopes below kMeanderSlope (fading from half of
// it). The gorge factor is smoothed over kGorgeSmooth points, fixed at both ends.
constexpr double kMeanderAmplitude = 1.5;
constexpr double kMeanderSlope = 0.003;
constexpr int kGorgeSmooth = 6;

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void measure(std::vector<CoursePoint>& points) {
    points.front().s = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        points[i].s = points[i - 1].s + dmath::hypot(points[i].x - points[i - 1].x,
                                                      points[i].z - points[i - 1].z);
    }
}

// The relief on the axis.
void sampleGround(const Heightfield& relief, std::vector<CoursePoint>& points) {
    std::vector<double> xs(points.size());
    std::vector<double> zs(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        xs[i] = points[i].x;
        zs[i] = points[i].z;
    }
    std::vector<HeightSample> samples(points.size());
    relief.samplesAt(xs, zs, samples);
    for (std::size_t i = 0; i < points.size(); ++i) {
        points[i].ground = samples[i].height;
        points[i].mountain = std::min(samples[i].mountainMask, kMountainFull) / kMountainFull;
    }
}

// The water level from a down to b: never rising, at least b's, at most a's. The river cuts the
// sills it meets, never deeper than kPlainCut below the relief (kMountainCut under the
// mountains): the water follows the lowest relief met since a, but stands at least that much below
// the highest relief still ahead of it (a sill holds a pond behind it). Where the bed has to come
// down below the relief to reach b, it does so on a ramp of at least kRampSlope.
void setLevels(std::vector<CoursePoint>& points, float levelA, float levelB) {
    const double length = points.back().s;
    float lowest = levelA;
    for (std::size_t i = 1; i < points.size(); ++i) {
        lowest = std::min(lowest, points[i].ground);
    }
    const double excess = static_cast<double>(lowest) - static_cast<double>(levelB);
    const double ramp = excess > 0.0 && length > 0.0
                            ? std::max(kRampSlope, static_cast<double>(levelA - levelB) / length)
                            : 0.0;
    // The highest relief at and ahead of each point, less the cut there.
    std::vector<float> hold(points.size(), -std::numeric_limits<float>::infinity());
    for (std::size_t i = points.size() - 1; i > 0; --i) {
        const double cut = lerp(kPlainCut, kMountainCut, static_cast<double>(points[i].mountain));
        const float here = static_cast<float>(static_cast<double>(points[i].ground) - cut);
        hold[i] = i + 1 < points.size() ? std::max(hold[i + 1], here) : here;
    }
    float running = levelA;
    points.front().level = levelA;
    for (std::size_t i = 1; i < points.size(); ++i) {
        running = std::min(running, points[i].ground);
        float level = running;
        if (ramp > 0.0) {
            level = std::min(level, static_cast<float>(static_cast<double>(levelB) +
                                                       ramp * (length - points[i].s)));
        }
        level = std::max(level, hold[i]);
        points[i].level = std::clamp(level, levelB, levelA);
    }
    points.back().level = levelB;
}

// The stretches where the water falls, sampled every kFallStep meters.
bool refineFalls(const Heightfield& relief, std::vector<CoursePoint>& points) {
    std::vector<CoursePoint> out;
    bool refined = false;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        out.push_back(points[i]);
        const CoursePoint& a = points[i];
        const CoursePoint& b = points[i + 1];
        const double run = b.s - a.s;
        const float drop = a.level - b.level;
        if (!(drop > kFallDrop) || !(static_cast<double>(drop) > kFallSlope * run) ||
            !(run > 2.0 * kFallStep)) {
            continue;
        }
        const auto steps = static_cast<int>(std::ceil(run / kFallStep));
        for (int k = 1; k < steps; ++k) {
            const double t = static_cast<double>(k) / steps;
            CoursePoint p = a;
            p.x = lerp(a.x, b.x, t);
            p.z = lerp(a.z, b.z, t);
            out.push_back(p);
        }
        refined = true;
    }
    out.push_back(points.back());
    if (refined) {
        points = std::move(out);
        measure(points);
        sampleGround(relief, points);
    }
    return refined;
}

// The axis of the reach: smoothed, its end bent toward where it joins another river.
std::vector<CoursePoint> axisOf(const ReachInput& in) {
    const std::vector<PathPoint> axis = smoothAxis(in.before, *in.own, in.after);
    std::vector<CoursePoint> points(axis.size());
    for (std::size_t i = 0; i < axis.size(); ++i) {
        points[i].x = axis[i].x;
        points[i].z = axis[i].z;
    }
    measure(points);
    if (in.endAt) {
        const double dx = in.endAt->x - points.back().x;
        const double dz = in.endAt->z - points.back().z;
        const double length = points.back().s;
        for (CoursePoint& p : points) {
            const double w = smooth(length - kJoinFade, length, p.s);
            p.x += w * dx;
            p.z += w * dz;
        }
        points.back().x = in.endAt->x;
        points.back().z = in.endAt->z;
        measure(points);
    }
    return points;
}

// The gorge factor of each point from the relief above its water, smoothed along the course (but
// at both ends, which other reaches share).
void setGorges(std::vector<CoursePoint>& points) {
    std::vector<float> raw(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        const CoursePoint& p = points[i];
        const double cut = std::max(0.0f, p.ground - p.level);
        raw[i] = static_cast<float>(lerp(smooth(kCanyonLow, kCanyonHigh, cut),
                                         smooth(kGorgeLow, kGorgeHigh, cut),
                                         static_cast<double>(p.mountain)));
    }
    const auto n = static_cast<std::ptrdiff_t>(points.size());
    for (std::ptrdiff_t i = 0; i < n; ++i) {
        const std::ptrdiff_t room = std::min({std::ptrdiff_t{kGorgeSmooth}, i, n - 1 - i});
        float most = 0.0f;
        for (std::ptrdiff_t k = i - room; k <= i + room; ++k) {
            most = std::max(most, raw[static_cast<std::size_t>(k)]);
        }
        points[static_cast<std::size_t>(i)].gorge = most;
    }
}

double taperOf(const ReachInput& in, double distance) {
    double w = 1.0;
    if (in.taperStart) {
        w *= smooth(0.0, kMeanderTaper, distance);
    }
    if (in.taperEnd) {
        w *= smooth(0.0, kMeanderTaper, in.segmentLength - distance);
    }
    return w;
}

// The river and the shape of the valley at each point.
void setShapes(const ReachInput& in, std::vector<CoursePoint>& points) {
    const double length = std::max(points.back().s, 1.0);
    const double phaseRate = (in.b.phase - in.a.phase) / length;
    for (CoursePoint& p : points) {
        const double t = p.s / length;
        const double width = lerp(in.a.width, in.b.width, t);
        const double discharge = std::max(10.0, lerp(in.a.discharge, in.b.discharge, t));
        const double slope = lerp(in.a.slope, in.b.slope, t);
        const double m = static_cast<double>(p.mountain);
        const double g = static_cast<double>(p.gorge);
        p.half = static_cast<float>(0.5 * width);
        p.depth = static_cast<float>(lerp(in.a.depth, in.b.depth, t));
        p.phase = in.a.phase + phaseRate * p.s;
        p.phaseRate = static_cast<float>(phaseRate);
        const double flat = 1.0 - smooth(0.5 * kMeanderSlope, kMeanderSlope, slope);
        const double meander = kMeanderAmplitude * width * (1.0 - m) * (1.0 - g) * flat *
                               taperOf(in, lerp(in.a.distance, in.b.distance, t));
        p.meander = static_cast<float>(meander);
        const double plainFloor = 0.5 * width + meander + kPlainMargin * width;
        const double uFloor = std::max(0.5 * width + 20.0,
                                       kUFloorBase + kUFloorGrowth * dmath::log(discharge / 10.0) /
                                                         dmath::log(10.0));
        p.floorHalf = static_cast<float>(lerp(lerp(plainFloor, uFloor, m), 0.5 * width + kGorgeFloor, g));
        p.floorSlope = static_cast<float>(lerp(lerp(kPlainFloorSlope, kUFloorSlope, m), 0.0, g));
        p.wall = static_cast<float>(lerp(lerp(kPlainWall, kUWall, m), lerp(kCanyonWall, kGorgeWall, m), g));
        p.foot = static_cast<float>(lerp(lerp(kPlainFoot, kUFoot, m), kGorgeFoot, g));
    }
}

// The chunks of 8 segments and the box of the course.
constexpr std::size_t kChunk = 8;

void setChunks(Course& c) {
    const std::vector<CoursePoint>& pts = c.points;
    c.minX = c.maxX = pts.front().x;
    c.minZ = c.maxZ = pts.front().z;
    for (std::size_t first = 0; first + 1 < pts.size(); first += kChunk) {
        const std::size_t last = std::min(first + kChunk, pts.size() - 1);
        CourseChunk k;
        k.first = static_cast<std::uint32_t>(first);
        k.last = static_cast<std::uint32_t>(last);
        k.minLevel = std::numeric_limits<float>::infinity();
        k.minWall = std::numeric_limits<float>::infinity();
        for (std::size_t i = first; i <= last; ++i) {
            k.x += pts[i].x / static_cast<double>(last - first + 1);
            k.z += pts[i].z / static_cast<double>(last - first + 1);
        }
        for (std::size_t i = first; i <= last; ++i) {
            const CoursePoint& p = pts[i];
            k.radius = std::max(k.radius, dmath::hypot(p.x - k.x, p.z - k.z));
            k.flat = std::max({k.flat,
                               static_cast<double>(p.floorHalf + p.foot) + wallWarpOf(p.floorHalf),
                               1.5 * static_cast<double>(p.half + p.meander)});
            k.minLevel = std::min(k.minLevel, p.level);
            k.maxDepth = std::max(k.maxDepth, p.depth);
            k.minWall = std::min({k.minWall, p.wall, static_cast<float>(0.5 * kBankSlope)});
            c.minX = std::min(c.minX, p.x);
            c.maxX = std::max(c.maxX, p.x);
            c.minZ = std::min(c.minZ, p.z);
            c.maxZ = std::max(c.maxZ, p.z);
        }
        c.chunks.push_back(k);
    }
    c.minX -= kReach;
    c.minZ -= kReach;
    c.maxX += kReach;
    c.maxZ += kReach;
}

} // namespace

Course buildCourse(const Heightfield& relief, const ReachInput& in) {
    Course c;
    c.points = axisOf(in);
    sampleGround(relief, c.points);
    setLevels(c.points, in.a.level, in.b.level);
    if (refineFalls(relief, c.points)) {
        setLevels(c.points, in.a.level, in.b.level);
    }
    setGorges(c.points);
    setShapes(in, c.points);
    setChunks(c);
    float highest = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < c.points.size(); ++i) {
        const CoursePoint& p = c.points[i];
        c.deepestCut = std::max(c.deepestCut, p.ground - p.level);
        if (p.ground > highest) {
            highest = p.ground;
            c.crest = p.s;
        }
        if (i > 0) {
            c.steepest = std::max(c.steepest, c.points[i - 1].level - p.level);
        }
    }
    return c;
}

} // namespace terrain::water
