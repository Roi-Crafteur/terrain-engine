#include "tableland_canyons.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain::tableland_relief {

namespace {

// The canyons: the zero lines of a noise of kCanyonWavelength, their branches those of a noise of
// kBranchWavelength from kBranchFar to kBranchNear meters of them, kBranchDepth as deep (hanging
// over them), where a mask of kMaskWavelength passes kMaskLow to kMaskHigh. Their floor meets the
// low ground at the escarpment and rises kInland of the plateau over kReach meters inland. A canyon
// gets shallow where it runs along the escarpment within kAlongFar meters of it (the sine of their
// directions from kAcrossHigh to kAcrossLow) or its mask fades: its head.
constexpr double kCanyonWavelength = 40'000.0;
constexpr double kBranchWavelength = 12'000.0;
constexpr double kBranchNear = 2'000.0;
constexpr double kBranchFar = 6'000.0;
constexpr double kBranchDepth = 0.55;
constexpr double kMaskWavelength = 60'000.0;
constexpr double kMaskLow = -0.2;
constexpr double kMaskHigh = 0.2;
constexpr double kInland = 0.65;
constexpr double kReach = 40'000.0;
constexpr double kAcrossLow = 0.3;
constexpr double kAcrossHigh = 0.7;
constexpr double kAlongNear = 4'000.0;
constexpr double kAlongFar = 8'000.0;
// Their walls run kWidest of their wavelength at most (steeper beyond): the distance to their axis,
// the value of the noise over its gradient, holds that far. Their axes meander by up to kMeander
// meters (a warp of kMeanderWavelength: the entrenched meanders of the Goosenecks).
constexpr double kWidest = 0.1;
constexpr double kMeander = 700.0;
constexpr double kMeanderWavelength = 5'000.0;
// The fields are measured by differences over these steps (meters), the gradients of the lines
// read no smaller than kLeastLine of the mean gradient of their noise (about 2 per wavelength):
// where two of them cross, a confluence, never a pinch.
constexpr double kCanyonStep = 80.0;
constexpr double kBranchStep = 30.0;
constexpr double kTempleStep = 20.0;
constexpr double kLeastLine = 0.3;
// The course of a canyon (how it runs to the escarpment) is read on its noise without its
// meanders, by differences over kCourseStep: its depth follows its course, never each bend.
constexpr double kCourseStep = 400.0;

// Their section, from the axis out: a floor kFloorLeast + kFloorShare of the depth wide on each
// side; an inner gorge of slope kInnerSlope; a platform kPlatformLow to kPlatformHigh meters wide (a
// noise of kPlatformWavelength), of slope kPlatformSlope; a great cliff kGreatLow to kGreatHigh
// meters high (a noise of kGreatWavelength); upper slopes kUpperShare of the depth at the slope of
// the walls (kWallHumid to kWallArid); a caprock kCapShare of the depth, kCapLeast to kCapMost
// meters, kCapRoom of the depth at most. The great cliff and the platform come in as the depth
// passes kUnitsLow to kUnitsHigh (the shallower canyons: a caprock over their slopes). Toward its
// head, as its depth falls below kHeadDeep, its floor narrows out and its walls lie back by up to
// kHeadSwale meters more: a swale, never a trench. The strata leave its floor and the lowest
// kFloorWall of the segment over it.
constexpr double kFloorLeast = 40.0;
constexpr double kFloorShare = 0.12;
constexpr double kInnerSlope = 0.6;
constexpr double kPlatformLow = 300.0;
constexpr double kPlatformHigh = 1'500.0;
constexpr double kPlatformWavelength = 25'000.0;
constexpr double kPlatformSlope = 0.05;
constexpr double kGreatLow = 120.0;
constexpr double kGreatHigh = 200.0;
constexpr double kGreatWavelength = 30'000.0;
constexpr double kUpperShare = 0.25;
constexpr double kWallHumid = 0.3;
constexpr double kWallArid = 0.45;
constexpr double kCapShare = 0.08;
constexpr double kCapLeast = 40.0;
constexpr double kCapMost = 120.0;
constexpr double kCapRoom = 0.4;
constexpr double kUnitsLow = 500.0;
constexpr double kUnitsHigh = 800.0;
constexpr double kHeadDeep = 150.0;
constexpr double kHeadSwale = 150.0;
constexpr double kFloorWall = 0.2;
// Their rims recede by the jag of the escarpments, kJagMost meters at most (tableland_relief.cpp),
// half the run of the walls at most.
constexpr double kJagMost = 750.0;

// The temples: on the platform and the lower half of the upper slopes, the blobs of a noise of
// kTempleWavelength over kTempleThreshold, their tops kTempleDropLow to kTempleDropHigh of the depth
// under the rim (a noise of kTempleDropWavelength), their talus of slope kTempleTalus.
constexpr double kTempleWavelength = 3'500.0;
constexpr double kTempleThreshold = 0.1;
constexpr double kTempleDropLow = 0.1;
constexpr double kTempleDropHigh = 0.3;
constexpr double kTempleDropWavelength = 15'000.0;
constexpr double kTempleTalus = 0.8;

// The segments of half a section, from the axis out.
enum Segment : std::size_t { kFloor, kInner, kPlatform, kGreat, kUpper, kCap, kSegments };

struct Section {
    std::array<double, kSegments> width{};
    std::array<double, kSegments> drop{};
    double depth = 0.0;
    double units = 0.0; // [0, 1]: the great cliff and the platform there
};

double total(const Section& s) {
    double sum = 0.0;
    for (const double w : s.width) {
        sum += w;
    }
    return sum;
}

// The section of a canyon deep meters deep, its walls of slope wall, at most widest meters wide on
// each side, head of its swale, great and platform its noises.
Section sectionOf(double deep, double wall, double widest, double head, double great,
                  double platform) {
    Section s;
    s.depth = deep;
    s.units = smooth(kUnitsLow, kUnitsHigh, deep);
    const double cap = std::min(std::clamp(kCapShare * deep, kCapLeast, kCapMost), kCapRoom * deep);
    const double hg = s.units * (kGreatLow + (kGreatHigh - kGreatLow) * smooth(-0.5, 0.5, great));
    const double wp =
        s.units * (kPlatformLow + (kPlatformHigh - kPlatformLow) * smooth(-0.5, 0.5, platform));
    const double hu = s.units * kUpperShare * deep + (1.0 - s.units) * (deep - cap);
    const double dp = kPlatformSlope * wp;
    const double hi = std::max(0.0, deep - cap - hu - hg - dp);
    s.drop = {0.0, hi, dp, hg, hu, cap};
    s.width = {kFloorLeast * (1.0 - head) + kFloorShare * deep, hi / kInnerSlope, wp,
               hg / kCliffSlope, hu / wall + kHeadSwale * head, cap / kCliffSlope};
    // No wider than widest: the platform narrows first (its drop going to the inner gorge), then
    // the slopes steepen.
    const double over = total(s) - widest;
    if (over > 0.0) {
        const double narrower = std::max(0.0, s.width[kPlatform] - over);
        const double moved = s.drop[kPlatform] - kPlatformSlope * narrower;
        s.width[kPlatform] = narrower;
        s.drop[kPlatform] -= moved;
        s.drop[kInner] += moved;
        s.width[kInner] += moved / kInnerSlope;
    }
    const double still = total(s) - widest;
    if (still > 0.0) {
        const double slopes = s.width[kInner] + s.width[kUpper];
        const double k = std::max(0.1, (slopes - still) / slopes);
        s.width[kInner] *= k;
        s.width[kUpper] *= k;
    }
    return s;
}

// Where a point s meters from the axis lies on a section: its depth, the slope there, its segment
// and how far into it (0 at its inner edge).
struct Along {
    double depth = 0.0;
    double slope = 0.0;
    std::size_t segment = kSegments;
    double into = 1.0;
};

Along alongOf(const Section& s, double distance) {
    double start = 0.0;
    double depth = s.depth;
    for (std::size_t k = 0; k < kSegments; ++k) {
        const double w = s.width[k];
        if (distance < start + w) {
            const double t = (distance - start) / w;
            return {depth - s.drop[k] * t, s.drop[k] / w, k, t};
        }
        start += w;
        depth -= s.drop[k];
    }
    return {};
}

// The start of a segment, meters from the axis.
double startOf(const Section& s, std::size_t segment) {
    double start = 0.0;
    for (std::size_t k = 0; k < segment; ++k) {
        start += s.width[k];
    }
    return start;
}

// The hard layer of a wall at a column depth meters under the rim: its caprock in the upper half
// of its walls, its great cliff under it.
Unit unitOf(const Section& s, double depth) {
    const double cap = s.drop[kCap];
    const double upper = s.drop[kUpper];
    const double great = s.drop[kGreat];
    if (great > 0.0 && depth > cap + 0.5 * upper) {
        return {cap + upper, great, false};
    }
    return {kUnderTop, cap, true};
}

// The noise f of a canyon at (x, z), its axis meandering.
double meandering(const CanyonNoises& n, const noise::FbmOctaves& f, double x, double z) {
    const WorldSize& w = n.world;
    const double cx = noise::wrapCanonical(x, w.x);
    const double cz = noise::wrapCanonical(z, w.z);
    const std::array<float, 2> warp = n.meanderX.fbm2DPair(n.meanderZ, cx, cz);
    return wrapped(f, w, cx + kMeander * static_cast<double>(warp[0]),
                   cz + kMeander * static_cast<double>(warp[1]));
}

// A canyon along the zero line of line at a column (course: its noise without its meanders): its
// section, the distance to its axis.
struct Canyon {
    Section section;
    double distance = 0.0; // meters from the axis, the jag of the rim added
    double side = 1.0;     // the sign of the line: the side of the axis
    Field line;
};

// The canyon along line, depth meters deep at full strength (strength, its mask), walls of slope
// wall: shallow toward its head (where it runs along the escarpment, or its strength falls).
Canyon canyonOf(const CanyonNoises& n, double x, double z, const Field& line, const Field& course,
                const Field& scarp, double depth, double wall, double strength, double jag,
                double widest) {
    Canyon c;
    c.line = line;
    const double across = std::abs(course.dirX * scarp.dirZ - course.dirZ * scarp.dirX);
    const double deep = depth * strength *
                        std::max(smooth(kAcrossLow, kAcrossHigh, across),
                                 smooth(kAlongNear, kAlongFar, scarp.value));
    if (!(deep > 0.0)) {
        return c;
    }
    const double head = 1.0 - smooth(0.0, kHeadDeep, deep);
    c.section = sectionOf(deep, wall, widest, head, static_cast<double>(n.great.fbm2D(x, z)),
                          static_cast<double>(n.platform.fbm2D(x, z)));
    const double floor = c.section.width[kFloor];
    const double run = total(c.section) - floor;
    double d = std::abs(line.value);
    if (d > floor) {
        // Rising with the distance whatever the jag (at most half the run of the walls).
        d += std::min(1.0, (d - floor) / run) * jag * std::min(1.0, 0.5 * run / kJagMost);
    }
    c.distance = d;
    c.side = line.value < 0.0 ? -1.0 : 1.0;
    return c;
}

// The cut of a canyon at its column.
Cut cutOf(const Canyon& c) {
    Cut out;
    if (!(c.section.depth > 0.0)) {
        return out;
    }
    const Along a = alongOf(c.section, c.distance);
    if (a.segment == kSegments) {
        return out;
    }
    out.depth = a.depth;
    out.slope = a.slope;
    const double rate = -a.slope * c.side;
    out.gradX = rate * c.line.dirX;
    out.gradZ = rate * c.line.dirZ;
    out.floor = a.segment == kFloor ? 1.0
                : a.segment == kInner || (a.segment == kUpper && c.section.units < 0.5)
                    ? 1.0 - smooth(0.0, kFloorWall, a.into)
                    : 0.0;
    out.unit = unitOf(c.section, a.depth);
    out.cliff = a.segment == kCap || a.segment == kGreat;
    return out;
}

// The temples of a deep canyon at (x, z): where they stand, their cut takes the place of that of
// the canyon over the inner gorge (whose cut stays: the floor stays drained).
void addTemples(const CanyonNoises& n, double x, double z, const Canyon& c, double cliff,
                Cut& cut) {
    const Section& s = c.section;
    if (!(s.units > 0.0) || !(cut.depth > 0.0)) {
        return;
    }
    const double inner = startOf(s, kPlatform);
    const double upper = startOf(s, kUpper);
    const double window = smooth(inner, inner + 0.25 * s.width[kPlatform], c.distance) *
                          (1.0 - smooth(upper + 0.3 * s.width[kUpper],
                                        upper + 0.6 * s.width[kUpper], c.distance));
    if (!(window > 0.0)) {
        return;
    }
    const double platform = s.depth - s.drop[kInner]; // the depth of its inner edge
    const double drop = s.depth * (kTempleDropLow + (kTempleDropHigh - kTempleDropLow) *
                                                        smooth(-0.4, 0.4, static_cast<double>(
                                                                              n.templeDrop.fbm2D(x, z))));
    const double height = platform - drop;
    if (!(height > 0.0)) {
        return;
    }
    const WorldSize& w = n.world;
    const Field blob = distanceOf(
        [&n, &w](double u, double v) { return wrapped(n.temples, w, u, v) - kTempleThreshold; },
        x, z, kTempleStep);
    const Storeys t = storeysAt(blob.value, height, cliff * height, kTempleTalus);
    const double held = window * t.share;
    const double depth = drop + (1.0 - held) * height;
    const double upperCut = std::min(cut.depth, platform);
    if (!(depth < upperCut)) {
        return;
    }
    const double gorge = std::max(0.0, cut.depth - platform);
    cut.depth = depth + gorge;
    const double rate = -height * window * t.rate;
    cut.gradX = rate * blob.dirX;
    cut.gradZ = rate * blob.dirZ;
    cut.slope = height * window * t.rate;
    cut.floor = 0.0;
    cut.unit = {drop + kUnderTop, cliff * height, true};
    cut.cliff = t.cliff;
    cut.temple = true;
}

noise::FbmOctaves octaves(std::uint64_t seed, std::uint64_t stream, double wavelength, int count,
                          const WorldSize& world,
                          noise::OctaveLattices lattices = noise::OctaveLattices::RotatedFast) {
    return noise::FbmOctaves(
        noise::deriveSeed(seed, stream),
        {.octaves = count, .frequency = 1.0 / wavelength, .lattices = lattices}, world);
}

} // namespace

CanyonNoises makeCanyonNoises(std::uint64_t seed, const WorldSize& world) {
    constexpr noise::OctaveLattices kTwins = noise::OctaveLattices::Unrotated;
    return {octaves(seed, 4, kCanyonWavelength, 2, world),
            octaves(seed, 5, kBranchWavelength, 2, world),
            octaves(seed, 18, kMeanderWavelength, 2, world, kTwins),
            octaves(seed, 19, kMeanderWavelength, 2, world, kTwins),
            octaves(seed, 6, kMaskWavelength, 2, world),
            octaves(seed, 20, kGreatWavelength, 2, world),
            octaves(seed, 21, kPlatformWavelength, 2, world),
            octaves(seed, 22, kTempleWavelength, 2, world),
            octaves(seed, 23, kTempleDropWavelength, 2, world),
            world};
}

Cut canyonsAt(const CanyonNoises& n, double x, double z, double thickness, const Field& scarp,
              double arid, double jag, double cliff) {
    const double mask = smooth(kMaskLow, kMaskHigh, static_cast<double>(n.mask.fbm2D(x, z)));
    if (!(mask > 0.0)) {
        return {};
    }
    const double depth = thickness * (1.0 - kInland * smooth(0.0, kReach, scarp.value));
    const double wall = mix(kWallHumid, kWallArid, arid);
    const Field main =
        distanceOf([&n](double u, double v) { return meandering(n, n.lines, u, v); }, x, z,
                   kCanyonStep, kLeastLine * 2.0 / kCanyonWavelength);
    const WorldSize& w = n.world;
    const Field course = distanceOf(
        [&n, &w](double u, double v) { return wrapped(n.lines, w, u, v); }, x, z, kCourseStep);
    const Canyon canyon = canyonOf(n, x, z, main, course, scarp, depth, wall, mask, jag,
                                   kWidest * kCanyonWavelength);
    Cut out = cutOf(canyon);
    addTemples(n, x, z, canyon, cliff, out);
    const double near = 1.0 - smooth(kBranchNear, kBranchFar, std::abs(main.value));
    if (near > 0.0) {
        const Field branch =
            distanceOf([&n](double u, double v) { return meandering(n, n.branches, u, v); }, x, z,
                       kBranchStep, kLeastLine * 2.0 / kBranchWavelength);
        const Field branchCourse = distanceOf(
            [&n, &w](double u, double v) { return wrapped(n.branches, w, u, v); }, x, z,
            kCourseStep);
        const Cut hanging =
            cutOf(canyonOf(n, x, z, branch, branchCourse, scarp, kBranchDepth * depth, wall,
                           mask * near, jag, kWidest * kBranchWavelength));
        if (hanging.depth > out.depth) {
            out = hanging;
        }
    }
    return out;
}

} // namespace terrain::tableland_relief
