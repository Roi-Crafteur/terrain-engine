#include "tableland_relief.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace terrain::tableland_relief {

namespace {

// The other forms give way to the plateau as its province passes 0 to kCalmHigh; its strata stand
// as it passes kRockLow to kRockHigh (the other forms gone); its thickness comes in from kRiseLow
// to kRiseHigh, at the far margin of its outliers (its escarpment lies within, kScarpBias).
constexpr float kCalmHigh = 0.1f;
constexpr double kRockLow = 0.1;
constexpr double kRockHigh = 0.25;
constexpr double kRiseLow = 0.1;
constexpr double kRiseHigh = 0.3;

// The thickness of the plateau over the low ground: kThickLow + kThickRange u^1.5 meters, u from 0
// to 1 as a noise of kThickWavelength passes -kThickEdge to kThickEdge (the cliffs of the Grand
// Staircase to the walls of the Grand Canyon).
constexpr double kThickWavelength = 700'000.0;
constexpr double kThickLow = 150.0;
constexpr double kThickRange = 1650.0;
constexpr double kThickEdge = 0.45;

// The escarpment: the zero line of a noise of kScarpWavelength warped by up to kScarpWarp meters (a
// noise of kScarpWarpWavelength), plus kScarpBias times the share of the province over
// kScarpShare: the plateau fills the heart of its province (no hole in it), its escarpment wanders
// across its edge, where the share passes about 0.35 to 0.85.
constexpr double kScarpWavelength = 48'000.0;
constexpr int kScarpOctaves = 3;
constexpr double kScarpWarpWavelength = 30'000.0;
constexpr double kScarpWarp = 12'000.0;
constexpr double kScarpBias = 2.0;
constexpr double kScarpShare = 0.6;

// The outliers: the blobs of a noise of kOutlierWavelength over a threshold rising from
// kOutlierFrom at the escarpment to kOutlierTo at the retreat, kRetreatHumid to kRetreatArid
// meters out (Monument Valley lies some 15 km out of its escarpments); their tops lower as they
// lie farther, down to 1 - kOutlierLow of the plateau at the retreat (the upper layers stripped),
// and past kOutlierNear of the retreat no higher than kCapLow to kCapHigh meters (the thicker the
// formations, the taller: Monument Valley's buttes stand about 300 m), the promontories at the
// escarpment as high as it.
constexpr double kOutlierWavelength = 4'000.0;
constexpr double kOutlierFrom = -0.1;
constexpr double kOutlierTo = 0.6;
constexpr double kOutlierLow = 0.75;
constexpr double kOutlierNear = 0.3;
constexpr double kCapLow = 150.0;
constexpr double kCapHigh = 450.0;
// The rims of the escarpments and of the outliers jut out and recede by up to kJag meters (a noise
// of kJagWavelength): re-entrants and points, no straight rim over kilometers.
constexpr double kJag = 250.0;
constexpr double kJagWavelength = 2'000.0;
constexpr double kRetreatHumid = 6'000.0;
constexpr double kRetreatArid = 15'000.0;

// The mean slope of the talus before the rock lays its cliffs and ledges on it (meters per meter):
// concave, 1.5 times that under the rim, flat at the foot.
constexpr double kTalusHumid = 0.45;
constexpr double kTalusArid = 0.8;
// The gullies: kGullySpacings meters apart, kGullyDepths of the drop over their spacing deep, as
// the slope passes kGullyFlat to kGullySteep, never deeper than kGullyRoom of the rise.
constexpr std::array<double, 2> kGullySpacings = {256.0, 128.0};
constexpr std::array<float, 2> kGullyDepths = {0.3f, 0.22f};
constexpr double kGullyFlat = 0.1;
constexpr double kGullySteep = 0.4;
constexpr double kGullyRoom = 0.5;

// The canyons: the zero lines of a noise of kCanyonWavelength, their branches those of a noise of
// kBranchWavelength from kBranchFar to kBranchNear meters of them, kBranchDepth as deep (hanging
// over them), where a mask of kMaskWavelength passes kMaskLow to kMaskHigh. Their floor meets the
// low ground at the escarpment and rises kInland of the plateau over kReach meters inland; it is
// kFloorLeast + kFloorShare of their depth wide on each side of the axis, flat, their walls of
// slope kWallHumid to kWallArid before the rock. A canyon gets shallow and narrow where it runs
// along the escarpment (the sine of their directions from kAcrossHigh to kAcrossLow) or its mask
// fades, its floor rises to the plateau: its head, never a long thin groove.
constexpr double kCanyonWavelength = 40'000.0;
constexpr double kBranchWavelength = 12'000.0;
// Their walls run kWidest of their wavelength at most (steeper beyond): the distance to their axis,
// the value of the noise over its gradient, holds that far. Their axes meander by up to kMeander
// meters (a warp of kMeanderWavelength: the entrenched meanders of the Goosenecks).
constexpr double kWidest = 0.1;
constexpr double kMeander = 700.0;
constexpr double kMeanderWavelength = 5'000.0;
constexpr double kBranchNear = 2'000.0;
constexpr double kBranchFar = 6'000.0;
constexpr double kBranchDepth = 0.55;
constexpr double kMaskWavelength = 60'000.0;
constexpr double kMaskLow = -0.2;
constexpr double kMaskHigh = 0.2;
constexpr double kInland = 0.65;
constexpr double kReach = 40'000.0;
constexpr double kFloorLeast = 40.0;
constexpr double kFloorShare = 0.12;
constexpr double kWallHumid = 0.3;
constexpr double kWallArid = 0.45;
constexpr double kAcrossLow = 0.3;
constexpr double kAcrossHigh = 0.7;
// Only near the escarpment (within kAlongNear to kAlongFar meters of it): farther inland they run
// any way.
constexpr double kAlongNear = 4'000.0;
constexpr double kAlongFar = 8'000.0;
// Toward its head, as its depth falls below kHeadDeep, its floor narrows out and its walls lie back
// by up to kHeadSwale meters more: a swale, never a trench.
constexpr double kHeadDeep = 150.0;
constexpr double kHeadSwale = 150.0;
// The strata leave the floors of the canyons (up to kFloorWall of their walls) and the foot of the
// talus (up to kFootRock of the profile): flat floors, smooth aprons.
constexpr double kFloorWall = 0.2;
constexpr double kFootRock = 0.15;

// The fields are measured by differences over these steps (meters), their gradients no smaller
// than kLeastGradient (per meter).
constexpr double kOutlierStep = 20.0;
constexpr double kCanyonStep = 80.0;
constexpr double kBranchStep = 30.0;
constexpr double kLeastGradient = 1e-9;

// The wavelengths of the noises of the rock (tableland_strata.cpp) and of the relief of the tops,
// which fades out over the outer kTopEdge of the profile and the first kTopCut of the cut of a
// canyon.
constexpr double kBandWavelength = 20'000.0;
constexpr double kDipWavelength = 200'000.0;
constexpr double kRimWavelength = 300.0;
constexpr double kTopFineWavelength = 900.0;
constexpr double kTopCoarseWavelength = 5'000.0;
constexpr double kTopEdge = 0.2;
constexpr double kTopCut = 0.1;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double mix(double humid, double arid, double share) {
    return humid + (arid - humid) * share;
}

// A signed distance in meters (positive within) and the unit gradient of its field.
struct Field {
    double value = 0.0;
    double dirX = 1.0;
    double dirZ = 0.0;
};

// The distance to the zero line of f (a field of the plane) at (x, z): its value over its
// gradient, by differences step meters long.
template <typename F>
Field distanceOf(const F& f, double x, double z, double step) {
    const double v = f(x, z);
    const double gradX = (f(x + step, z) - v) / step;
    const double gradZ = (f(x, z + step) - v) / step;
    const double g = std::max(dmath::hypot(gradX, gradZ), kLeastGradient);
    return {v / g, gradX / g, gradZ / g};
}

// The noise f of a canyon at (x, z), its axis meandering (Noises::meanderX, meanderZ).
double meandering(const Noises& n, const noise::FbmOctaves& f, double x, double z) {
    const WorldSize& w = n.world;
    const double cx = noise::wrapCanonical(x, w.x);
    const double cz = noise::wrapCanonical(z, w.z);
    const std::array<float, 2> warp = n.meanderX.fbm2DPair(n.meanderZ, cx, cz);
    return static_cast<double>(
        f.fbm2D(noise::wrapCanonical(cx + kMeander * static_cast<double>(warp[0]), w.x),
                noise::wrapCanonical(cz + kMeander * static_cast<double>(warp[1]), w.z)));
}

// The noise f at (x, z), wrapped onto the world.
double wrapped(const noise::FbmOctaves& f, const WorldSize& w, double x, double z) {
    return static_cast<double>(
        f.fbm2D(noise::wrapCanonical(x, w.x), noise::wrapCanonical(z, w.z)));
}

double scarpValue(const Noises& n, double x, double z, float share) {
    const double bias = kScarpBias * (static_cast<double>(share) - kScarpShare);
    const WorldSize& w = n.world;
    const double cx = noise::wrapCanonical(x, w.x);
    const double cz = noise::wrapCanonical(z, w.z);
    const std::array<float, 2> warp = n.scarpWarpX.fbm2DPair(n.scarpWarpZ, cx, cz);
    return wrapped(n.scarp, w, cx + kScarpWarp * static_cast<double>(warp[0]),
                   cz + kScarpWarp * static_cast<double>(warp[1])) +
           bias;
}

double thicknessAt(const Noises& n, double x, double z) {
    const double u = smooth(-kThickEdge, kThickEdge, static_cast<double>(n.thickness.fbm2D(x, z)));
    return kThickLow + kThickRange * u * std::sqrt(u);
}

// The escarpment at (x, z): the zero line of the noise biased by the share of the province, its
// gradient measured with the shares around.
Field scarpAt(const Noises& n, double x, double z, const Province& q) {
    const double v = scarpValue(n, x, z, q.point.tableland);
    const double gradX = (scarpValue(n, x + kShareStep, z, q.alongX) - v) / kShareStep;
    const double gradZ = (scarpValue(n, x, z + kShareStep, q.alongZ) - v) / kShareStep;
    const double g = std::max(dmath::hypot(gradX, gradZ), kLeastGradient);
    return {v / g, gradX / g, gradZ / g};
}

// The cut of a canyon.
struct Cut {
    double depth = 0.0; // meters
    double gradX = 0.0; // of the depth
    double gradZ = 0.0;
    double floor = 0.0; // [0, 1]: on its floor
};

// The cut of a canyon along the zero line of line, depth meters deep on its axis at full strength,
// its walls of slope wall: shallow and narrow toward its head, where it runs along the escarpment
// or its strength falls;
// its rim jutting out and receding by jag meters (none at the foot of its walls: its floor stays
// as it drains).
Cut cutOf(const Field& line, const Field& scarp, double depth, double wall, double strength,
          double jag, double widest) {
    const double across = std::abs(line.dirX * scarp.dirZ - line.dirZ * scarp.dirX);
    const double deep = depth * strength *
                        std::max(smooth(kAcrossLow, kAcrossHigh, across),
                                 smooth(kAlongNear, kAlongFar, scarp.value));
    if (!(deep > 0.0)) {
        return {};
    }
    const double head = 1.0 - smooth(0.0, kHeadDeep, deep);
    const double floor = kFloorLeast * (1.0 - head) + kFloorShare * deep;
    const double walls = std::clamp(deep / wall, 1.0, widest) + kHeadSwale * head;
    double q = (std::abs(line.value) - floor) / walls; // 0 at the foot, 1 at the rim
    if (q > 0.0) {
        // Rising with q whatever the jag (at most half the run of the walls).
        q += std::min(q, 1.0) * jag * std::min(1.0, 0.5 * walls / kJag) / walls;
    }
    Cut out;
    if (!(q < 1.0)) {
        return out;
    }
    if (!(q > 0.0)) {
        out.depth = deep;
        out.floor = 1.0;
        return out;
    }
    out.depth = deep * (1.0 - q);
    const double rate = -(deep / walls) * (line.value < 0.0 ? -1.0 : 1.0);
    out.gradX = rate * line.dirX;
    out.gradZ = rate * line.dirZ;
    out.floor = 1.0 - smooth(0.0, kFloorWall, q);
    return out;
}

// The canyons at (x, z) of a plateau thickness meters thick, scarp its escarpment.
Cut canyonsAt(const Noises& n, double x, double z, double thickness, const Field& scarp,
              double arid, double jag) {
    const double mask = smooth(kMaskLow, kMaskHigh, static_cast<double>(n.canyonMask.fbm2D(x, z)));
    if (!(mask > 0.0)) {
        return {};
    }
    const double depth = thickness * (1.0 - kInland * smooth(0.0, kReach, scarp.value));
    const double wall = mix(kWallHumid, kWallArid, arid);
    const Field main = distanceOf(
        [&n](double u, double v) { return meandering(n, n.canyons, u, v); }, x, z, kCanyonStep);
    Cut out = cutOf(main, scarp, depth, wall, mask, jag, kWidest * kCanyonWavelength);
    const double near = 1.0 - smooth(kBranchNear, kBranchFar, std::abs(main.value));
    if (near > 0.0) {
        const Field branch =
            distanceOf([&n](double u, double v) { return meandering(n, n.branches, u, v); }, x, z,
                       kBranchStep);
        const Cut hanging = cutOf(branch, scarp, kBranchDepth * depth, wall, mask * near, jag,
                                  kWidest * kBranchWavelength);
        if (hanging.depth > out.depth) {
            out = hanging;
        }
    }
    return out;
}

// The fields of a point and what the shape needs besides: the field of the outline the top holds
// to, how much it rises (the plateau the canyons leave, an outlier lower), the cut.
struct Parts {
    Fields fields;
    Field outline;
    double held = 0.0;
    Cut cut;
};

// The top an outline holds to: its distance outline, its talus talus meters wide, concave (u^1.5:
// 1.5 times its mean slope under the rim).
double profileOf(double outline, double talus) {
    const double u = std::clamp(1.0 + outline / talus, 0.0, 1.0);
    return u * std::sqrt(u);
}

// The outliers beyond the escarpment scarp at (x, z) of a plateau thickness meters thick: their
// outline (none where the threshold leaves none) and the height of their tops.
struct Outliers {
    Field outline;
    double height = 0.0;
};
Outliers outliersAt(const Noises& n, double x, double z, const Field& scarp, double thickness,
                    double arid) {
    Outliers out;
    const double retreat = mix(kRetreatHumid, kRetreatArid, arid);
    if (!(scarp.value < 0.0) || !(scarp.value > -retreat)) {
        return out;
    }
    const double far = -scarp.value / retreat;
    const double threshold = kOutlierFrom + (kOutlierTo - kOutlierFrom) * far;
    const WorldSize& w = n.world;
    out.outline = distanceOf(
        [&n, &w, threshold](double u, double v) { return wrapped(n.outliers, w, u, v) - threshold; },
        x, z, kOutlierStep);
    const double cap =
        kCapLow + (kCapHigh - kCapLow) * smooth(-0.5, 0.5, static_cast<double>(n.bands.fbm2D(x, z)));
    const double kept = (1.0 - kOutlierLow * far) * thickness;
    out.height = std::min(kept, cap + std::max(0.0, kept - cap) * (1.0 - smooth(0.0, kOutlierNear, far)));
    return out;
}

Parts measure(const Noises& n, double x, double z, const Province& q) {
    Parts out;
    const TablelandPoint& p = q.point;
    const double rising = smooth(kRiseLow, kRiseHigh, static_cast<double>(p.tableland));
    if (!(rising > 0.0)) {
        return out;
    }
    const auto arid = static_cast<double>(p.arid);
    Fields& f = out.fields;
    f.thickness = rising * thicknessAt(n, x, z);
    Field scarp = scarpAt(n, x, z, q);
    const double jag = kJag * static_cast<double>(n.jags.fbm2D(x, z));
    scarp.value += jag;
    const double slope = mix(kTalusHumid, kTalusArid, arid);
    f.scarp = scarp.value;
    f.talus = std::max(f.thickness / slope, 1.0);
    f.profile = profileOf(scarp.value, f.talus);
    f.outline = scarp.value;
    out.outline = scarp;
    Outliers outliers = outliersAt(n, x, z, scarp, f.thickness, arid);
    outliers.outline.value += jag;
    const double lower = outliers.height;
    const double outlierTalus = std::max(lower / slope, 1.0);
    const double outlierProfile =
        lower > 0.0 ? profileOf(outliers.outline.value, outlierTalus) : 0.0;
    if (!(std::max(f.profile, outlierProfile) > 0.0)) {
        return out;
    }
    out.cut = canyonsAt(n, x, z, f.thickness, scarp, arid, jag);
    f.canyon = out.cut.depth;
    f.floor = out.cut.floor;
    out.held = std::max(0.0, f.thickness - f.canyon);
    if (outlierProfile * std::min(lower, out.held) > f.profile * out.held) {
        out.held = std::min(lower, out.held);
        out.outline = outliers.outline;
        f.outline = outliers.outline.value;
        f.talus = outlierTalus;
        f.profile = outlierProfile;
    }
    return out;
}

noise::FbmOctaves octaves(std::uint64_t seed, std::uint64_t stream, double wavelength, int count,
                          const WorldSize& world,
                          noise::OctaveLattices lattices = noise::OctaveLattices::RotatedFast) {
    return noise::FbmOctaves(
        noise::deriveSeed(seed, stream),
        {.octaves = count, .frequency = 1.0 / wavelength, .lattices = lattices}, world);
}

} // namespace

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    constexpr noise::OctaveLattices kTwins = noise::OctaveLattices::Unrotated;
    return {octaves(seed, 0, kScarpWavelength, kScarpOctaves, world),
            octaves(seed, 1, kScarpWarpWavelength, 2, world, kTwins),
            octaves(seed, 2, kScarpWarpWavelength, 2, world, kTwins),
            octaves(seed, 3, kOutlierWavelength, 3, world),
            octaves(seed, 16, kJagWavelength, 3, world),
            octaves(seed, 4, kCanyonWavelength, 2, world),
            octaves(seed, 5, kBranchWavelength, 2, world),
            octaves(seed, 18, kMeanderWavelength, 2, world, kTwins),
            octaves(seed, 19, kMeanderWavelength, 2, world, kTwins),
            octaves(seed, 6, kMaskWavelength, 2, world),
            octaves(seed, 7, kThickWavelength, 2, world),
            octaves(seed, 8, kBandWavelength, 2, world),
            octaves(seed, 9, kDipWavelength, 2, world),
            octaves(seed, 10, kRimWavelength, 2, world),
            octaves(seed, 11, kTopFineWavelength, 3, world),
            octaves(seed, 12, kTopCoarseWavelength, 2, world),
            noise::deriveSeed(seed, 13),
            noise::deriveSeed(seed, 14),
            noise::deriveSeed(seed, 17),
            ravines::makeNetwork(noise::deriveSeed(seed, 15), world, kGullySpacings, kGullyDepths),
            world};
}

float calm(const TablelandPoint& p) {
    return p.tableland > 0.0f ? noise::smoothstep(0.0f, kCalmHigh, p.tableland) : 0.0f;
}

Fields fieldsAt(const Noises& n, double x, double z, const Province& q) {
    return measure(n, x, z, q).fields;
}

Shape shapeAt(const Noises& n, double x, double z, const Province& q) {
    const TablelandPoint& p = q.point;
    const Parts parts = measure(n, x, z, q);
    const Fields& f = parts.fields;
    Shape out;
    if (!(f.profile > 0.0)) {
        return out;
    }
    // The plateau the canyons leave (an outlier lower), raised by the profile from the low ground.
    const double held = parts.held;
    out.rise = f.profile * held;
    // The profile u^1.5 of the outline it holds to (profileOf): its slope 1.5 u^0.5 per talus.
    const double u = std::clamp(1.0 + f.outline / f.talus, 0.0, 1.0);
    const double ramp = f.profile < 1.0 ? 1.5 * std::sqrt(u) * held / f.talus : 0.0;
    out.gradX = ramp * parts.outline.dirX - f.profile * parts.cut.gradX;
    out.gradZ = ramp * parts.outline.dirZ - f.profile * parts.cut.gradZ;
    // The gullies down the taluses and the walls of the canyons.
    const double gullies =
        smooth(kGullyFlat, kGullySteep, dmath::hypot(out.gradX, out.gradZ));
    if (gullies > 0.0) {
        const auto depth = static_cast<double>(
            ravines::depth(n.gullies, x, z, out.gradX, out.gradZ, gullies));
        out.rise -= std::min(depth, kGullyRoom * out.rise);
    }
    out.rock = smooth(kRockLow, kRockHigh, static_cast<double>(p.tableland)) *
               smooth(0.0, kFootRock, f.profile) * (1.0 - f.floor);
    out.thickness = f.thickness;
    out.top = smooth(1.0 - kTopEdge, 1.0, f.profile) *
              (1.0 - smooth(0.0, kTopCut, f.canyon / std::max(f.thickness, 1.0)));
    return out;
}

} // namespace terrain::tableland_relief
