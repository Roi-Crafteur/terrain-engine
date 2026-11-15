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
// Staircase to the walls of the Grand Canyon), swelling by up to kSwell meters (a noise of
// kSwellWavelength): the skyline of a rim rises and falls.
constexpr double kThickWavelength = 700'000.0;
constexpr double kThickLow = 150.0;
constexpr double kThickRange = 1650.0;
constexpr double kThickEdge = 0.45;
constexpr double kSwell = 40.0;
constexpr double kSwellWavelength = 8'000.0;

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
// Its distance is the value of the field over its gradient: a distance near the escarpment only
// (out of the share of the province its gradient falls several times within kilometers, where the
// outliers stand). How far out they stand is read on the value of the field itself, kScarpRate of it
// to a meter (its usual gradient at the escarpment): smooth, its gradient known.
constexpr double kScarpRate = 8.0e-5;

// The outliers: the blobs of a noise of kOutlierWavelength (buttes) and of one of kMesaWavelength
// (mesas), mixed by one of kMixWavelength, over a threshold rising from kOutlierFrom at the
// escarpment to kOutlierTo at the retreat and moved by up to kDensity (a noise of
// kDensityWavelength: dense fields, sparse ones). The retreat runs kRetreatLeast to kRetreatMost
// meters out (a noise of kRetreatWavelength; half as far in the humid lands: Monument Valley lies
// some 15 km out of its escarpments). Their tops lower as they lie farther, down to
// 1 - kOutlierLow of the plateau at the retreat (the upper layers stripped), and past kOutlierNear
// of the retreat no higher than kCapLow to kCapHigh meters (the thicker the formations, the
// taller: Monument Valley's buttes stand about 300 m), the promontories at the escarpment as high
// as it.
constexpr double kOutlierWavelength = 2'500.0;
constexpr double kMesaWavelength = 9'000.0;
constexpr double kMixWavelength = 60'000.0;
constexpr double kOutlierFrom = -0.1;
constexpr double kOutlierTo = 0.6;
constexpr double kDensity = 0.7;
constexpr double kDensityWavelength = 20'000.0;
constexpr double kRetreatLeast = 6'000.0;
constexpr double kRetreatMost = 25'000.0;
constexpr double kRetreatWavelength = 80'000.0;
constexpr double kOutlierLow = 0.75;
constexpr double kOutlierNear = 0.3;
constexpr double kCapLow = 150.0;
constexpr double kCapHigh = 450.0;
// The rims of the escarpments and of the outliers jut out and recede by up to kJag meters (a noise
// of kJagWavelength) and kJagFine more (one of kJagFineWavelength): bays and points of every size
// (their cliffs rough besides, layer by layer: tableland_cliffs.hpp). The escarpment bends by up
// to kBend meters (a noise of kBendWavelength, 3 octaves: bays and capes of 3 to 12 km) and by its
// swell (tableland_front.hpp: bendAt): no long straight front; its slope under 1, never folding.
constexpr double kBend = 6'000.0;
constexpr double kBendWavelength = 24'000.0;
constexpr double kJag = 500.0;
constexpr double kJagWavelength = 3'000.0;
constexpr double kJagFine = 400.0;
constexpr double kJagFineWavelength = 600.0;
// The caprocks take kCliffLow to kCliffHigh of the height of a top (a noise of kCliffWavelength),
// kScarpCliffMost meters of an escarpment at most; under them a concave talus of mean slope
// kTalusHumid to kTalusArid before the rock lays its ledges on it.
constexpr double kCliffLow = 0.45;
constexpr double kCliffHigh = 0.65;
constexpr double kCliffWavelength = 30'000.0;
constexpr double kScarpCliffMost = 300.0;
constexpr double kTalusHumid = 0.45;
constexpr double kTalusArid = 0.8;
// The gullies: kGullySpacings meters apart, kGullyDepths of the drop over their spacing deep, as
// the slope passes kGullyFlat to kGullySteep (read kGullySlopeMost at most), none on the cliffs
// (from kGullyCliffLow to kGullyCliffHigh), never deeper than kGullyRoom of the rise.
constexpr std::array<double, 2> kGullySpacings = {256.0, 128.0};
constexpr std::array<float, 2> kGullyDepths = {0.3f, 0.22f};
constexpr double kGullyFlat = 0.1;
constexpr double kGullySteep = 0.4;
constexpr double kGullySlopeMost = 0.8;
constexpr double kGullyCliffLow = 1.5;
constexpr double kGullyCliffHigh = 3.0;
constexpr double kGullyRoom = 0.5;
constexpr double kGullyFoot = 30.0;
// The strata leave the foot of the talus (up to kFootRock of the profile): smooth aprons. The
// forced hard layers of a wall come in over the kUnitFoot meters of rise over its foot (none under
// it: their lowest one, partial, would lay its ledge into the talus); those of a great cliff give
// way kGreatAbove meters over its top.
constexpr double kFootRock = 0.15;
constexpr double kUnitFoot = 5.0;
constexpr double kGreatAbove = 20.0;
// The taluses of the outliers kSoftNear to kSoftFar meters out in front of the escarpment and
// farther are soft (Shape::soft).
constexpr double kSoftNear = 500.0;
constexpr double kSoftFar = 2'000.0;
// The fields are measured by differences over these steps (meters).
constexpr double kOutlierStep = 20.0;
// The offsets of the rims (their jags, the bends of the escarpment) move them by meters that change
// by up to about 1 per meter: near a rim (kStretchNear meters, fading out by kStretchFar), the
// distance to it is read over the gradient of its field and of its offset (by differences over
// kOffsetStep meters, kLeastStretch at least): its profile keeps its widths in meters; farther, the
// meters of the offset are only added.
constexpr double kOffsetStep = 4.0;
constexpr double kLeastStretch = 0.5;
constexpr double kStretchNear = 500.0;
constexpr double kStretchFar = 1'500.0;
// The wavelengths of the noises of the rock (tableland_strata.cpp) and of the relief of the tops,
// which fades out over the outer kTopEdge of the profile and the first kTopCut of the cut of a
// canyon (a share of the height of the outline it cuts: an outlier too).
constexpr double kBandWavelength = 20'000.0;
constexpr double kMemberWavelength = 30'000.0;
constexpr double kBedWavelength = 25'000.0;
constexpr double kDipWavelength = 200'000.0;
constexpr double kRimWavelength = 300.0;
constexpr double kTopFineWavelength = 900.0;
constexpr double kTopCoarseWavelength = 5'000.0;
constexpr double kTopEdge = 0.2;
constexpr double kTopCut = 0.1;

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

// The offset of a rim at a point, meters, and its gradient.
struct Offset {
    double value = 0.0;
    double gradX = 0.0;
    double gradZ = 0.0;
};

// The offset f (a function of the canonical plane, meters) at (x, z) and its gradient.
template <typename F>
Offset offsetOf(const F& f, const WorldSize& w, double x, double z) {
    const auto at = [&f, &w](double u, double v) {
        return f(noise::wrapCanonical(u, w.x), noise::wrapCanonical(v, w.z));
    };
    const double v = at(x, z);
    return {v, (at(x + kOffsetStep, z) - v) / kOffsetStep, (at(x, z + kOffsetStep) - v) / kOffsetStep};
}

// The field f moved by the offset o, its distance read over their gradient together near its rim.
Field offsetField(const Field& f, const Offset& o) {
    const double gx = f.dirX + o.gradX;
    const double gz = f.dirZ + o.gradZ;
    const double g = dmath::hypot(gx, gz);
    const double norm = std::max(g, kLeastGradient);
    const double moved = f.value + o.value;
    const double near = 1.0 - smooth(kStretchNear, kStretchFar, std::abs(moved));
    return {moved * (1.0 + near * (1.0 / std::max(g, kLeastStretch) - 1.0)), gx / norm, gz / norm};
}

double thicknessAt(const Noises& n, double x, double z) {
    const double u = smooth(-kThickEdge, kThickEdge, static_cast<double>(n.thickness.fbm2D(x, z)));
    return kThickLow + kThickRange * u * std::sqrt(u) +
           kSwell * static_cast<double>(n.swell.fbm2D(x, z));
}

// The escarpment at (x, z): the zero line of the noise biased by the share of the province, its
// distance (field) and its value and gradient themselves, the gradient measured with the shares
// around.
struct Scarp {
    Field field;
    double value = 0.0;
    double gradX = 0.0;
    double gradZ = 0.0;
};
Scarp scarpAt(const Noises& n, double x, double z, const Province& q) {
    const double v = scarpValue(n, x, z, q.point.tableland);
    const double gradX = (scarpValue(n, x + kShareStep, z, q.alongX) - v) / kShareStep;
    const double gradZ = (scarpValue(n, x, z + kShareStep, q.alongZ) - v) / kShareStep;
    const double g = std::max(dmath::hypot(gradX, gradZ), kLeastGradient);
    return {{v / g, gradX / g, gradZ / g}, v, gradX, gradZ};
}

// The outliers beyond the escarpment scarp at (x, z) of a plateau thickness meters thick: their
// outline (none where the threshold leaves none) and the height of their tops.
struct Outliers {
    Field outline;
    double height = 0.0;
};
// The outliers beyond the escarpment s at (x, z), its rims moved by offset meters.
Outliers outliersAt(const Noises& n, double x, double z, const Scarp& s, const Offset& offset,
                    double thickness, double arid) {
    Outliers out;
    const double reach = smooth(-0.4, 0.4, static_cast<double>(n.retreat.fbm2D(x, z)));
    const double retreat =
        (kRetreatLeast + (kRetreatMost - kRetreatLeast) * reach) * mix(0.5, 1.0, arid);
    // How far out, in [0, 1] of the retreat.
    const double far = -(s.value + kScarpRate * offset.value) / (kScarpRate * retreat);
    if (!(far > 0.0) || !(far < 1.0)) {
        return out;
    }
    // The density moves the threshold less and less out to the retreat, where it reaches kOutlierTo,
    // over the noises but in rare places: the outliers end before it, seldom cut by it.
    const double threshold = kOutlierFrom + (kOutlierTo - kOutlierFrom) * far +
                             kDensity * static_cast<double>(n.density.fbm2D(x, z)) * (1.0 - far);
    // The outline: the blobs over the threshold, their distance read over the gradient of the blobs
    // (the threshold moves slowly).
    const double mesas = smooth(-0.3, 0.3, static_cast<double>(n.outlierMix.fbm2D(x, z)));
    const WorldSize& w = n.world;
    out.outline = distanceOf(
        [&n, &w, threshold, mesas](double u, double v) {
            return (1.0 - mesas) * wrapped(n.outliers, w, u, v) + mesas * wrapped(n.mesas, w, u, v) -
                   threshold;
        },
        x, z, kOutlierStep);
    const double cap =
        kCapLow + (kCapHigh - kCapLow) * smooth(-0.5, 0.5, static_cast<double>(n.bands.fbm2D(x, z)));
    const double kept = (1.0 - kOutlierLow * far) * thickness;
    out.height = std::min(kept, cap + std::max(0.0, kept - cap) * (1.0 - smooth(0.0, kOutlierNear, far)));
    return out;
}

// The fields of a point and what the shape needs besides: the outline the top holds to (its field,
// its profile of two storeys, the height of its top and of its caprock), how much it rises (the
// plateau the canyons leave, an outlier lower), the cut.
struct Parts {
    Fields fields;
    Field outline;
    Storeys storeys;
    double top = 0.0;   // of the outline, over the low ground
    double cliff = 0.0; // of its caprock
    double held = 0.0;
    double steep = 12.0; // the batter of the cliffs
    double rough = 0.0;  // meters the rough outline of the cliffs juts out
    Cut cut;
};

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
    out.rough = roughAt(n.walls, x, z);
    out.steep = steepAt(n.walls, x, z);
    const double jag = canyonJagAt(n.jags, x, z);
    // The offsets of the rims: their jags (their rough outline per layer: cliffStoreysAt); and the
    // bends of the escarpment.
    const Offset rims = offsetOf(
        [&n](double u, double v) {
            return kJag * static_cast<double>(n.jags.fbm2D(u, v)) +
                   kJagFine * static_cast<double>(n.jagsFine.fbm2D(u, v));
        },
        n.world, x, z);
    const Offset bends = offsetOf(
        [&n](double u, double v) {
            return kBend * static_cast<double>(n.bends.fbm2D(u, v)) +
                   tableland_front::bendAt(n.bendSwell, u, v);
        },
        n.world, x, z);
    const Scarp raw = scarpAt(n, x, z, q);
    const Offset offset{rims.value + bends.value, rims.gradX + bends.gradX,
                        rims.gradZ + bends.gradZ};
    const Field scarp = offsetField(raw.field, offset);
    f.scarp = scarp.value;
    const double talus = mix(kTalusHumid, kTalusArid, arid);
    const double share =
        kCliffLow + (kCliffHigh - kCliffLow) * smooth(-0.5, 0.5, static_cast<double>(n.cliffs.fbm2D(x, z)));
    const double scarpCliff = std::min(share * f.thickness, kScarpCliffMost);
    const Storeys onScarp =
        cliffStoreysAt(n.walls, n.units, x, z, scarp, f.thickness, scarpCliff, talus,
                       ledgesAt(n.walls, n.units, x, z, scarpCliff, out.steep));
    Outliers outliers = outliersAt(n, x, z, raw, offset, f.thickness, arid);
    outliers.outline = offsetField(outliers.outline, rims);
    const double outlierCliff = share * outliers.height;
    const Storeys onOutlier =
        cliffStoreysAt(n.walls, n.units, x, z, outliers.outline, outliers.height, outlierCliff,
                       talus, ledgesAt(n.walls, n.units, x, z, outlierCliff, out.steep));
    if (!(std::max(onScarp.share, onOutlier.share) > 0.0)) {
        return out;
    }
    // The canyons read the escarpment by its distance and its smooth direction (the course of a canyon
    // against it: never the normals of the jags of its rim, which change every few meters), how far
    // inland they run by the share of the province (from that of the escarpment to the heart).
    out.cut = canyonsAt(n.canyon, x, z, f.thickness,
                        smooth(kScarpShare, 1.0, static_cast<double>(q.point.tableland)),
                        {scarp.value, raw.field.dirX, raw.field.dirZ},
                        {arid, jag, share, out.steep, &n.walls, &n.jags});
    f.canyon = out.cut.depth;
    f.axis = out.cut.axis;
    f.gorge = out.cut.gorge;
    f.templeOutline = out.cut.templeOutline;
    f.floor = out.cut.floor;
    f.temple = out.cut.temple;
    out.held = std::max(0.0, f.thickness - f.canyon);
    out.outline = scarp;
    out.storeys = onScarp;
    out.top = f.thickness;
    out.cliff = scarpCliff;
    if (onOutlier.share * std::min(outliers.height, out.held) > onScarp.share * out.held) {
        out.held = std::min(outliers.height, out.held);
        out.outline = outliers.outline;
        out.storeys = onOutlier;
        out.top = outliers.height;
        out.cliff = share * outliers.height;
        out.fields.outlier = true;
    }
    out.fields.outline = out.outline.value;
    out.fields.cliff = out.cliff;
    out.fields.height = out.top;
    out.fields.talus = out.cliff / out.steep + (out.top - out.cliff) / talus;
    out.fields.profile = out.storeys.share;
    return out;
}

// The gullies down the taluses and the walls of the canyons (none in the caprocks): they lower the
// rise of s.
void addGullies(const Noises& n, double x, double z, const Parts& parts, Shape& s) {
    if (parts.storeys.cliff) {
        return;
    }
    const double slope = dmath::hypot(s.gradX, s.gradZ);
    const double strength = smooth(kGullyFlat, kGullySteep, slope) *
                            (1.0 - smooth(kGullyCliffLow, kGullyCliffHigh, slope));
    if (!(strength > 0.0)) {
        return;
    }
    const double read = slope > kGullySlopeMost ? kGullySlopeMost / slope : 1.0;
    // They come in over the first kGullyFoot meters of fall of the talus under a caprock (no step).
    const double fall = parts.held * (1.0 - parts.cliff / parts.top) - s.rise;
    const double fade = parts.cliff > 0.0 ? smooth(0.0, kGullyFoot, fall) : 1.0;
    const auto depth = static_cast<double>(
        ravines::depth(n.gullies, x, z, read * s.gradX, read * s.gradZ, strength * fade));
    s.rise -= std::min(depth, kGullyRoom * s.rise);
}

// The scree on the talus under a caprock, none in the canyons (tableland_cliffs.hpp): its blocks,
// and its cones at rest from the foot of the cliff (the talus raised to their surface where it lies
// lower: a fan softening its upper part, rising into the re-entrants of the outline), no block over
// the foot (fading out over its first meter). It raises the rise of s.
void addScree(const Noises& n, double x, double z, const Parts& parts, Shape& s) {
    if (!(parts.storeys.share > 0.0) || !(parts.storeys.share < 1.0) ||
        parts.fields.canyon > 0.0 || !(parts.top > 0.0)) {
        return;
    }
    const double foot = -parts.outline.value - parts.cliff / parts.steep;
    const Scree scree = screeAt(n.walls, x, z, foot, parts.cliff, parts.rough);
    const double footRise = parts.held * (1.0 - parts.cliff / parts.top);
    const double fan = std::max(0.0, footRise - kScreeSlope * foot - s.rise);
    s.rise += scree.cone * fan + scree.blocks * (1.0 - smooth(footRise, footRise + 1.0, s.rise));
}

// The hard layer of the wall at the column: the caprock of its outline on its cliff and the tops (none
// on its talus, under its foot), lowered with its profile by a canyon (Parts::held, as addScree: no
// step of it in soft strata); on the walls of a canyon in the plateau, the unit of the canyon there. On
// a top, it drapes the column (kUnderTop under its ground at most: no swale flattened to its foot).
void setUnit(const Parts& parts, double share, Shape& s) {
    const Fields& f = parts.fields;
    if (parts.storeys.share >= 1.0 && f.canyon > 0.0 && !f.outlier && parts.cut.bench > 0.0) {
        s.floorTop = f.thickness - parts.cut.bench;
    }
    double top = parts.held - kUnderTop;
    double height = parts.cliff * (parts.held / parts.top);
    bool capped = true;
    bool cliff = parts.storeys.cliff;
    if (parts.storeys.share >= 1.0 && f.canyon > 0.0 && !f.outlier) {
        top = f.thickness - parts.cut.unit.top;
        height = parts.cut.unit.height;
        capped = parts.cut.unit.capped;
        cliff = parts.cut.cliff;
    }
    if (!(height > 0.0)) {
        return;
    }
    if (!cliff && capped && parts.storeys.share >= 1.0) {
        top = std::min(top, s.rise - kUnderTop);
    }
    // A caprock of several layers comes in over its foot (unitBench); the great cliff, whole, under it.
    const double over = s.rise - (top - height);
    double weight = capped ? smooth(0.0, kUnitFoot, over) : smooth(-kUnitFoot, 0.0, over);
    if (!capped) {
        weight *= 1.0 - smooth(top, top + kGreatAbove, s.rise);
    }
    s.unitTop = top;
    s.unitHeight = height;
    s.unitWhole = !capped;
    // Whole wherever the plateau holds a caprock, whatever the share of its province (its shape fades
    // out with its thickness at the margins: a caprock half laid would blur its ledges into its slope).
    static_cast<void>(share);
    s.unitCover = weight * (1.0 - f.floor);
    s.unitWeight = s.unitCover;
}

noise::FbmOctaves octaves(std::uint64_t seed, std::uint64_t stream, double wavelength, int count,
                          const WorldSize& world,
                          noise::OctaveLattices lattices = noise::OctaveLattices::RotatedFast) {
    return noise::FbmOctaves(
        noise::deriveSeed(seed, stream),
        {.octaves = count, .frequency = 1.0 / wavelength, .lattices = lattices}, world);
}

} // namespace

double canyonJagAt(const noise::FbmOctaves& jags, double x, double z) {
    return kJag * static_cast<double>(jags.fbm2D(x, z));
}

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    constexpr noise::OctaveLattices kTwins = noise::OctaveLattices::Unrotated;
    return {octaves(seed, 0, kScarpWavelength, kScarpOctaves, world),
            octaves(seed, 1, kScarpWarpWavelength, 2, world, kTwins),
            octaves(seed, 2, kScarpWarpWavelength, 2, world, kTwins),
            octaves(seed, 3, kOutlierWavelength, 3, world),
            octaves(seed, 24, kMesaWavelength, 3, world),
            octaves(seed, 25, kMixWavelength, 2, world),
            octaves(seed, 26, kRetreatWavelength, 2, world),
            octaves(seed, 27, kDensityWavelength, 2, world),
            octaves(seed, 47, kBendWavelength, 3, world),
            tableland_front::makeBendSwell(noise::deriveSeed(seed, 48), world),
            octaves(seed, 16, kJagWavelength, 3, world),
            octaves(seed, 28, kJagFineWavelength, 3, world),
            octaves(seed, 32, kCliffWavelength, 2, world),
            makeCanyonNoises(seed, world),
            makeCliffNoises(seed, world),
            octaves(seed, 7, kThickWavelength, 2, world),
            octaves(seed, 29, kSwellWavelength, 2, world),
            octaves(seed, 8, kBandWavelength, 2, world),
            octaves(seed, 30, kMemberWavelength, 2, world),
            octaves(seed, 31, kBedWavelength, 2, world),
            octaves(seed, 9, kDipWavelength, 2, world),
            octaves(seed, 10, kRimWavelength, 2, world),
            octaves(seed, 11, kTopFineWavelength, 3, world),
            octaves(seed, 12, kTopCoarseWavelength, 2, world),
            noise::deriveSeed(seed, 13),
            noise::deriveSeed(seed, 14),
            noise::deriveSeed(seed, 17),
            noise::deriveSeed(seed, 33),
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
    const auto share = static_cast<double>(q.point.tableland);
    const Parts parts = measure(n, x, z, q);
    const Fields& f = parts.fields;
    Shape out;
    if (!(f.profile > 0.0)) {
        return out;
    }
    // The plateau the canyons leave (an outline lower), raised by the profile from the low ground.
    out.rise = parts.storeys.share * parts.held;
    const double ramp = parts.storeys.rate * parts.held;
    out.gradX = ramp * parts.outline.dirX - parts.storeys.share * parts.cut.gradX;
    out.gradZ = ramp * parts.outline.dirZ - parts.storeys.share * parts.cut.gradZ;
    addGullies(n, x, z, parts, out);
    addScree(n, x, z, parts, out);
    out.rock = smooth(kRockLow, kRockHigh, share) * smooth(0.0, kFootRock, f.profile) *
               (1.0 - f.floor);
    out.thickness = f.thickness;
    out.soft = smooth(kSoftNear, kSoftFar, -f.scarp);
    const double uncut = 1.0 - smooth(0.0, kTopCut, f.canyon / std::max(parts.top, 1.0));
    out.top = smooth(1.0 - kTopEdge, 1.0, f.profile) * uncut;
    setUnit(parts, share, out);
    const bool lowered = f.canyon > 0.0 && !f.outlier && parts.storeys.share < 1.0;
    out.lowered = lowered ? Shape::Lowered{parts.held, uncut} : Shape::Lowered{};
    return out;
}

} // namespace terrain::tableland_relief
