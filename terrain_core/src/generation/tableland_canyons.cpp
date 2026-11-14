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
// low ground at the escarpment and rises kInland of the plateau inland (over the share of the
// province: smooth, never the leaps of the distance to the escarpment far from it). A canyon
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
constexpr double kInland = 0.25;
constexpr double kAcrossLow = 0.3;
constexpr double kAcrossHigh = 0.7;
constexpr double kAlongNear = 4'000.0;
constexpr double kAlongFar = 8'000.0;
// Their walls run kWidest of their wavelength at most (steeper beyond): the distance to their axis,
// the value of the noise over its gradient, holds that far. Their axes meander by up to kMeander
// meters (kMeanderWavelength, 3 octaves down to 1.25 km for the branches, a warp of their noise; 2
// down to 2.5 km for the main ones: the entrenched meanders of the Goosenecks, never a straight
// canal). The meanders of the main ones are a shift across their smooth axis read at the foot of
// each column on it (measured along it over kWiggleStep meters): their whole section shifts with it,
// each of its lines a graph over the smooth axis (no fold, no island, no wall squeezed far from the
// axis, as a warp of the noise would).
constexpr double kWidest = 0.1;
constexpr double kMeander = 700.0;
constexpr double kMeanderWavelength = 5'000.0;
// The axes of the main ones bend besides by up to kWiggle meters (a noise of kWiggleWavelength, 3
// octaves: down to 350 m), read so: their floor, inner gorge and platform shift across with it, the
// shift fading out over kWiggleFade meters beyond the platform (the upper walls keep their slopes):
// no straight stretch of a few kilometers.
constexpr double kWiggle = 500.0;
constexpr double kWiggleStep = 10.0;
constexpr double kWiggleWavelength = 1'400.0;
constexpr double kWiggleFade = 1'200.0;
// The fields are measured by differences over these steps (meters), the gradients of the lines
// read no smaller than kLeastLine of the mean gradient of their noise (about 2 per wavelength):
// where two of them cross, a confluence, never a pinch.
constexpr double kCanyonStep = 80.0;
constexpr double kBranchStep = 30.0;
constexpr double kLeastLine = 0.3;
// The course of a canyon (how it runs to the escarpment) is read on its noise without its
// meanders, by differences over kCourseStep: its depth follows its course, never each bend.
constexpr double kCourseStep = 400.0;

// Their section, from the axis out: a floor kFloorLeast + kFloorShare of the depth wide on each
// side, kFloorNarrow to kFloorWide of that along the axis (a noise of kGorgeWavelength); an inner
// gorge of slope kInnerSteep to kInnerGentle with it (a wide floor, gentle walls); a platform
// kPlatformLow to kPlatformHigh meters wide (a noise of kPlatformWavelength), of slope
// kPlatformSlope; a great cliff kGreatLow to kGreatHigh meters high (a noise of kGreatWavelength);
// upper slopes kUpperShare of the depth at the slope of the walls (kWallHumid to kWallArid); a
// caprock kCapShare of the depth, kCapLeast to kCapMost meters, kCapRoom of the depth at most. The
// great cliff and the platform come in as the depth passes kUnitsLow to kUnitsHigh (the shallower
// canyons: a caprock over their slopes). Toward its head, as its depth falls below kHeadDeep, its
// floor narrows out and its walls lie back by up to kHeadSwale meters more: a swale, never a
// trench. The strata leave its floor and the lowest kFloorWall of the segment over it.
constexpr double kFloorLeast = 40.0;
constexpr double kFloorShare = 0.12;
constexpr double kFloorNarrow = 0.2;
constexpr double kFloorWide = 2.0;
constexpr double kGorgeWavelength = 4'000.0;
constexpr double kInnerSteep = 1.1;
constexpr double kInnerGentle = 0.35;
constexpr double kPlatformLow = 300.0;
constexpr double kPlatformHigh = 1'500.0;
constexpr double kPlatformWavelength = 25'000.0;
constexpr double kPlatformSlope = 0.05;
constexpr double kGreatLow = 130.0;
constexpr double kGreatHigh = 200.0;
constexpr double kGreatWavelength = 30'000.0;
// The great cliff keeps a batter of its own: kGreatSteep (85 degrees, one face of the Redwall).
constexpr double kGreatSteep = 12.0;
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

// The noises of a section at a column: of its great cliff, of its platform, of its inner gorge.
struct SectionNoises {
    double great = 0.0;
    double platform = 0.0;
    double gorge = 0.0;
};

// The section of a canyon deep meters deep, its walls of slope wall and its cliffs of slope steep,
// at most widest meters wide on each side, head of its swale, v its noises.
Section sectionOf(double deep, double wall, double steep, double widest, double head,
                  const SectionNoises& v) {
    Section s;
    s.depth = deep;
    s.units = smooth(kUnitsLow, kUnitsHigh, deep);
    const double cap = std::min(std::clamp(kCapShare * deep, kCapLeast, kCapMost), kCapRoom * deep);
    const double hg = s.units * (kGreatLow + (kGreatHigh - kGreatLow) * smooth(-0.5, 0.5, v.great));
    const double wp =
        s.units * (kPlatformLow + (kPlatformHigh - kPlatformLow) * smooth(-0.5, 0.5, v.platform));
    const double gorge = smooth(-0.5, 0.5, v.gorge);
    const double inner = mix(kInnerSteep, kInnerGentle, gorge);
    const double hu = s.units * kUpperShare * deep + (1.0 - s.units) * (deep - cap);
    const double dp = kPlatformSlope * wp;
    const double hi = std::max(0.0, deep - cap - hu - hg - dp);
    s.drop = {0.0, hi, dp, hg, hu, cap};
    const double floor =
        kFloorLeast * (1.0 - head) + kFloorShare * deep * mix(kFloorNarrow, kFloorWide, gorge);
    s.width = {floor, hi / inner, wp, hg / kGreatSteep, hu / wall + kHeadSwale * head, cap / steep};
    // No wider than widest: the platform narrows first (its drop going to the inner gorge), then
    // the slopes steepen.
    const double over = total(s) - widest;
    if (over > 0.0) {
        const double narrower = std::max(0.0, s.width[kPlatform] - over);
        const double moved = s.drop[kPlatform] - kPlatformSlope * narrower;
        s.width[kPlatform] = narrower;
        s.drop[kPlatform] -= moved;
        s.drop[kInner] += moved;
        s.width[kInner] += moved / inner;
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

// The distance d from the axis of a canyon of section s moved by the jag of its rims: by none on
// its floor, by all of it at its rims, rising with the distance whatever the jag (at most half the
// run of the walls).
double jagged(const Section& s, double d, double jag) {
    const double floor = s.width[kFloor];
    if (!(d > floor)) {
        return d;
    }
    const double run = total(s) - floor;
    return d + std::min(1.0, (d - floor) / run) * jag * std::min(1.0, 0.5 * run / kJagMost);
}

// The axis of a canyon at a column: the distance to the zero line of its noise; its meander and its
// wiggle read at the foot of the column on it, and their gradients (along the axis).
struct Axis {
    Field main;
    double meander = 0.0;
    double meanderX = 0.0;
    double meanderZ = 0.0;
    double wiggle = 0.0;
    double wiggleX = 0.0;
    double wiggleZ = 0.0;
};

// The distance to the axis a, meandering, wiggled up to reach meters from it, the wiggle fading out
// over kWiggleFade meters beyond; its gradient (no unit vector: the section sheared where the axis
// turns fast, the rate of a depth along the distance its slope).
Field wiggled(const Axis& a, double reach) {
    const double base = a.main.value + a.meander;
    const double baseX = a.main.dirX + a.meanderX;
    const double baseZ = a.main.dirZ + a.meanderZ;
    const double t = std::clamp((std::abs(base) - reach) / kWiggleFade, 0.0, 1.0);
    const double fade = 1.0 - t * t * (3.0 - 2.0 * t);
    const double fading = -6.0 * t * (1.0 - t) / kWiggleFade; // of fade, per meter from the axis
    const double radial = 1.0 + a.wiggle * fading * (base < 0.0 ? -1.0 : 1.0);
    return {base + a.wiggle * fade, radial * baseX + fade * a.wiggleX,
            radial * baseZ + fade * a.wiggleZ};
}

// A canyon along the zero line of line at a column (course: its noise without its meanders): its
// section, the distance to its axis.
struct Canyon {
    Section section;
    double distance = 0.0; // meters from the axis, the jag of the rim added
    double side = 1.0;     // the sign of the line: the side of the axis
    Field line;
};

// The canyon along line, depth meters deep at full strength (strength, its mask), at most widest
// meters wide on each side, its rims rims: shallow toward its head (where it runs along the
// escarpment, or its strength falls).
Canyon canyonOf(const CanyonNoises& n, double x, double z, const Axis& axis, const Field& course,
                const Field& scarp, double depth, double strength, double widest,
                const Rims& rims) {
    Canyon c;
    c.line = wiggled(axis, 0.0);
    const double across = std::abs(course.dirX * scarp.dirZ - course.dirZ * scarp.dirX);
    const double deep = depth * strength *
                        std::max(smooth(kAcrossLow, kAcrossHigh, across),
                                 smooth(kAlongNear, kAlongFar, scarp.value));
    if (!(deep > 0.0)) {
        return c;
    }
    const double head = 1.0 - smooth(0.0, kHeadDeep, deep);
    const SectionNoises v{static_cast<double>(n.great.fbm2D(x, z)),
                          static_cast<double>(n.platform.fbm2D(x, z)),
                          static_cast<double>(n.gorge.fbm2D(x, z))};
    c.section = sectionOf(deep, mix(kWallHumid, kWallArid, rims.arid), rims.steep, widest, head, v);
    c.line = wiggled(axis, startOf(c.section, kPlatform));
    c.distance = jagged(c.section, std::abs(c.line.value), rims.jag);
    c.side = c.line.value < 0.0 ? -1.0 : 1.0;
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

// A shift of an axis read at its foot (footX, footZ): amplitude meters of the noise f, and its
// gradient along the axis (alongX, alongZ).
struct Shift {
    double value = 0.0;
    double gradX = 0.0;
    double gradZ = 0.0;
};
Shift shiftAt(const noise::FbmOctaves& f, const WorldSize& w, double amplitude, double footX,
              double footZ, double alongX, double alongZ) {
    const double here = amplitude * wrapped(f, w, footX, footZ);
    const double there =
        amplitude * wrapped(f, w, footX + kWiggleStep * alongX, footZ + kWiggleStep * alongZ);
    const double turn = (there - here) / kWiggleStep;
    return {here, turn * alongX, turn * alongZ};
}

// The axis of the main canyons at (x, z): the distance to the zero line of their smooth noise, their
// meander and wiggle at the foot of (x, z) on it.
Axis axisAt(const CanyonNoises& n, double x, double z) {
    const WorldSize& w = n.world;
    const Field main = distanceOf([&n, &w](double u, double v) { return wrapped(n.lines, w, u, v); },
                                  x, z, kCanyonStep, kLeastLine * 2.0 / kCanyonWavelength);
    const double footX = x - main.value * main.dirX;
    const double footZ = z - main.value * main.dirZ;
    const Shift meander = shiftAt(n.meanders, w, kMeander, footX, footZ, -main.dirZ, main.dirX);
    const Shift wiggle = shiftAt(n.wiggles, w, kWiggle, footX, footZ, -main.dirZ, main.dirX);
    return {main,         meander.value, meander.gradX, meander.gradZ,
            wiggle.value, wiggle.gradX,  wiggle.gradZ};
}

// The canyon at the center (x, z) of the site of a temple, deep meters deep there, its rims rims.
TempleSpan spanAt(const CanyonNoises& n, double x, double z, double deep, const Rims& rims) {
    const double u = noise::wrapCanonical(x, n.world.x);
    const double v = noise::wrapCanonical(z, n.world.z);
    const SectionNoises noises{static_cast<double>(n.great.fbm2D(u, v)),
                               static_cast<double>(n.platform.fbm2D(u, v)),
                               static_cast<double>(n.gorge.fbm2D(u, v))};
    const Section s = sectionOf(deep, mix(kWallHumid, kWallArid, rims.arid), rims.steep,
                                kWidest * kCanyonWavelength, 1.0 - smooth(0.0, kHeadDeep, deep),
                                noises);
    const Field line = wiggled(axisAt(n, u, v), startOf(s, kPlatform));
    const double side =
        (line.value < 0.0 ? -1.0 : 1.0) / std::max(dmath::hypot(line.dirX, line.dirZ), kLeastGradient);
    const double jag =
        rims.jags != nullptr ? canyonJagAt(*rims.jags, u, v) : rims.jag;
    return {jagged(s, std::abs(line.value), jag), startOf(s, kPlatform), startOf(s, kGreat),
            s.depth - s.drop[kInner], s.depth, side * line.dirX, side * line.dirZ};
}

// The temples of a deep canyon at (x, z) (tableland_temples.hpp): where one stands on its platform,
// its cut takes the place of that of the canyon over the platform (the cut under it stays: the
// floor stays drained).
void addTemples(const CanyonNoises& n, double x, double z, const Canyon& c, const Rims& rims,
                Cut& cut) {
    const Section& s = c.section;
    if (!(s.units > 0.0) || !(cut.depth > 0.0)) {
        return;
    }
    const TempleSites sites = templeSitesNear(n.temples, x, z, s.depth, rims.cliff, rims.steep);
    if (sites.count == 0) {
        return;
    }
    std::array<TempleSpan, kMostTempleSites> spans{};
    for (std::size_t k = 0; k < sites.count; ++k) {
        spans[k] = spanAt(n, sites.site[k].x, sites.site[k].z, s.depth, rims);
    }
    const Temple t =
        templeAt(n.temples, x, z, sites, spans, rims.cliff, rims.steep, *rims.walls);
    const double platform = s.depth - s.drop[kInner]; // the depth of its inner edge
    const double upperCut = std::min(cut.depth, platform);
    if (!t.stands || !(t.depth < upperCut)) {
        return;
    }
    cut.depth = t.depth + std::max(0.0, cut.depth - platform);
    cut.gradX = t.gradX;
    cut.gradZ = t.gradZ;
    cut.slope = dmath::hypot(t.gradX, t.gradZ);
    if (!t.skirt) {
        cut.floor = 0.0;
        cut.unit = t.unit;
    }
    cut.cliff = t.cliff;
    cut.temple = true;
    cut.templeOutline = t.outline;
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
            octaves(seed, 18, kMeanderWavelength, 3, world, kTwins),
            octaves(seed, 19, kMeanderWavelength, 3, world, kTwins),
            octaves(seed, 50, kMeanderWavelength, 2, world),
            octaves(seed, 48, kWiggleWavelength, 3, world),
            octaves(seed, 6, kMaskWavelength, 2, world),
            octaves(seed, 20, kGreatWavelength, 2, world),
            octaves(seed, 21, kPlatformWavelength, 2, world),
            octaves(seed, 46, kGorgeWavelength, 2, world),
            makeTempleNoises(seed, world),
            world};
}

Cut canyonsAt(const CanyonNoises& n, double x, double z, double thickness, double inland,
              const Field& scarp, const Rims& rims) {
    const double mask = smooth(kMaskLow, kMaskHigh, static_cast<double>(n.mask.fbm2D(x, z)));
    if (!(mask > 0.0)) {
        return {};
    }
    const double depth = thickness * (1.0 - kInland * inland);
    const WorldSize& w = n.world;
    const Axis main = axisAt(n, x, z);
    const Canyon canyon = canyonOf(n, x, z, main, main.main, scarp, depth, mask,
                                   kWidest * kCanyonWavelength, rims);
    Cut out = cutOf(canyon);
    addTemples(n, x, z, canyon, rims, out);
    out.axis = canyon.line.value;
    out.gorge = startOf(canyon.section, kPlatform) - canyon.distance;
    const double near = 1.0 - smooth(kBranchNear, kBranchFar, std::abs(out.axis));
    if (near > 0.0) {
        const Field branch =
            distanceOf([&n](double u, double v) { return meandering(n, n.branches, u, v); }, x, z,
                       kBranchStep, kLeastLine * 2.0 / kBranchWavelength);
        const Field branchCourse = distanceOf(
            [&n, &w](double u, double v) { return wrapped(n.branches, w, u, v); }, x, z,
            kCourseStep);
        Cut hanging = cutOf(canyonOf(n, x, z, Axis{branch}, branchCourse, scarp, kBranchDepth * depth,
                                     mask * near, kWidest * kBranchWavelength, rims));
        if (hanging.depth > out.depth) {
            hanging.axis = out.axis;
            hanging.gorge = out.gorge;
            out = hanging;
        }
    }
    return out;
}

} // namespace terrain::tableland_relief
