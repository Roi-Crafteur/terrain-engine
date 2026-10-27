#include "depression_forms.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace terrain::depressions {

namespace {

// The space of a basin is warped by up to kWarp meters (3 octaves from kWarpWavelength), its rim
// lobed by up to kLobes of the way (3 octaves from kLobeWavelength); its gentle sides reach its
// floor kFlat of the way from the rim to the center, the steep side kFlat + kSteep of it.
constexpr double kWarp = 9'000.0;
constexpr double kWarpWavelength = 30'000.0;
constexpr double kLobes = 0.25;
constexpr double kLobeWavelength = 14'000.0;
constexpr double kFlat = 0.4;
constexpr double kSteep = 0.45;
// The gradient of the place in a basin is read over kRun meters.
constexpr double kRun = 10.0;
// The floor counts as such kFloorBlend of the way (or meters on the steep side) past its edge.
constexpr double kFloorBlend = 0.05;
constexpr double kFloorMeters = 1'000.0;
// The cliffs: on the side whose direction is kTowardLow to kTowardHigh toward the steep side (a
// cosine), where their noise of kCliffWavelength passes -kCliffEdge to kCliffEdge (part of its
// length); kCliffLow to kCliffHigh meters high (at most kCliffShare of the depth there), their
// face kFaceShare of it, from 30 to 60 degrees (tangents kFaceLow to kFaceHigh, a noise of
// kFaceWavelength), over a talus of scree as steep as kTalus at its top.
constexpr double kTowardLow = 0.3;
constexpr double kTowardHigh = 0.7;
constexpr double kCliffWavelength = 12'000.0;
constexpr double kCliffEdge = 0.15;
constexpr double kCliffLow = 80.0;
constexpr double kCliffHigh = 200.0;
constexpr double kCliffShare = 0.8;
constexpr double kFaceShare = 0.75;
constexpr double kFaceLow = 0.577;
constexpr double kFaceHigh = 1.732;
constexpr double kFaceWavelength = 4'000.0;
constexpr double kTalus = 0.7;
// The edge of the cliffs lies up to kEdge meters inside the rim, a noise of kEdgeWavelength.
constexpr double kEdge = 180.0;
constexpr double kEdgeWavelength = 900.0;
// The gentle slope below the foot reaches the floor at least kFootRun meters from the foot.
constexpr double kFootRun = 500.0;
// Buttes detached before the foot: from kOutlierNear to kOutlierFar meters past it, where a noise
// of kOutlierWavelength passes kOutlierLow to kOutlierHigh (steep sides), kOutlierShare of the
// height of the cliff.
constexpr double kOutlierNear = 150.0;
constexpr double kOutlierFar = 2'500.0;
constexpr double kOutlierWavelength = 1'400.0;
constexpr double kOutlierShare = 0.6;
constexpr double kOutlierLow = 0.45;
constexpr double kOutlierHigh = 0.55;
// The ravines may cut kRoomFace of what the face of a cliff still has to drop, and kRoomSlope of
// the height above the floor on the slopes.
constexpr double kRoomFace = 0.7;
constexpr double kRoomSlope = 0.6;
// The floor: salt pans where a noise of kPanWavelength passes kPanLow to kPanHigh, buttes where one
// of kButteWavelength passes kButteLow to kButteHigh, kButteMin to kButteMax meters high (a noise
// of kHeightWavelength).
constexpr double kPanWavelength = 6'000.0;
constexpr double kPanLow = 0.22;
constexpr double kPanHigh = 0.3;
constexpr double kButteWavelength = 1'400.0;
constexpr double kButteLow = 0.42;
constexpr double kButteHigh = 0.52;
constexpr double kButteMin = 20.0;
constexpr double kButteMax = 80.0;
constexpr double kHeightWavelength = 4'000.0;
constexpr double kTwoPi = 6.283185307179586476925286766559;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The derivative of smooth along v.
double smoothSlope(double edge0, double edge1, double v) {
    const double t = (v - edge0) / (edge1 - edge0);
    if (!(t > 0.0 && t < 1.0)) {
        return 0.0;
    }
    return 6.0 * t * (1.0 - t) / (edge1 - edge0);
}

double fbm(const noise::FbmOctaves& n, double x, double z) {
    return static_cast<double>(n.fbm2D(x, z));
}

// Where a point lies in a basin: rho of the way to the rim (above 1 outside), delta meters inside
// it, the cosine of its direction from the steep side, the direction up toward the rim.
struct Place {
    double rho = 2.0;
    double radius = 1.0; // of the rim in that direction, meters
    double delta = 0.0;
    double toward = 0.0;
    double upX = 1.0;
    double upZ = 0.0;
};

Place placeOf(const Forms& forms, const Site& s, double x, double z, double dx, double dz) {
    const std::array<float, 2> warp = forms.warpX.fbm2DPair(forms.warpZ, x, z);
    dx += kWarp * static_cast<double>(warp[0]);
    dz += kWarp * static_cast<double>(warp[1]);
    const double c = dmath::cosTurns(s.angle);
    const double n = dmath::sinTurns(s.angle);
    const double along = dx * c + dz * n;
    const double across = dz * c - dx * n;
    const double root = std::sqrt(s.elongation);
    const double ellipse = dmath::hypot(along / (s.radius * root), across * root / s.radius);
    Place p;
    p.toward = dmath::cosTurns(dmath::atan2(dz, dx) / kTwoPi - s.steepSide);
    const double rim = 1.0 + kLobes * fbm(forms.outline, x, z) - s.escarpment * p.toward;
    p.rho = ellipse / rim;
    const double r = dmath::hypot(dx, dz);
    if (r > 0.0 && p.rho > 0.0) {
        p.radius = r / p.rho;
        p.upX = dx / r;
        p.upZ = dz / r;
    } else {
        p.radius = s.radius;
    }
    p.delta = p.radius - r;
    return p;
}

// The gentle profile: down from the rim to the floor kFlat of the way to the center.
Profile softProfile(const Place& p, double above) {
    Profile out;
    out.share = 1.0 - smooth(kFlat, 1.0, p.rho);
    out.slope = above * smoothSlope(kFlat, 1.0, p.rho) / p.radius;
    out.onFloor = 1.0 - smooth(kFlat, kFlat + kFloorBlend, p.rho);
    out.room = kRoomSlope * above * (1.0 - out.share);
    return out;
}

// The steep side with its cliff: the face, the talus, the buttes detached before the foot and the
// gentle slope down to the floor.
Profile cliffProfile(const Forms& forms, const Place& p, double x, double z, double above) {
    const double height =
        std::min(kCliffLow + (kCliffHigh - kCliffLow) * smooth(-0.4, 0.4, fbm(forms.heights, x, z)),
                 kCliffShare * above);
    const double tangent =
        kFaceLow + (kFaceHigh - kFaceLow) * smooth(-0.4, 0.4, fbm(forms.faces, x, z));
    const double face = kFaceShare * height;
    const double talus = height - face;
    const double faceRun = 1.5 * face / tangent;
    const double talusRun = 2.0 * talus / kTalus;
    const double foot = faceRun + talusRun;
    const double floorEdge = std::max(foot + kFootRun, p.radius * (1.0 - kFlat - kSteep));
    // The edge of the cliff in by up to kEdge meters from the rim: spurs and re-entrants.
    const double d = p.delta - kEdge * smooth(-0.6, 0.6, fbm(forms.edge, x, z));
    // Three overlapping smooth steps: the face, the talus from half way down the face, the rest
    // from the foot of the face; continuous in height and in slope (the ravines follow it).
    const double talusFrom = 0.5 * faceRun;
    const double faceDrop = face * smooth(0.0, faceRun, d);
    const double rest = above - height;
    double drop = faceDrop + talus * smooth(talusFrom, foot, d) +
                  rest * smooth(faceRun, floorEdge, d);
    double slope = face * smoothSlope(0.0, faceRun, d) + talus * smoothSlope(talusFrom, foot, d) +
                   rest * smoothSlope(faceRun, floorEdge, d);
    const double zone = smooth(foot + kOutlierNear - 100.0, foot + kOutlierNear, d) *
                        (1.0 - smooth(foot + 0.5 * kOutlierFar, foot + kOutlierFar, d));
    if (zone > 0.0) {
        // Lower than the cliff, standing on the slope below it.
        const double butte = zone * smooth(kOutlierLow, kOutlierHigh, fbm(forms.outliers, x, z));
        drop -= butte * kOutlierShare * height;
        slope *= 1.0 - butte;
    }
    Profile out;
    out.share = std::clamp(drop / above, 0.0, 1.0);
    out.slope = slope;
    out.onFloor = smooth(floorEdge, floorEdge + kFloorMeters, d);
    out.room = kRoomFace * (face - faceDrop) +
               kRoomSlope * (above - drop) * smooth(faceRun, foot, d);
    return out;
}

} // namespace

Forms makeForms(std::uint64_t seed, const WorldSize& world) {
    const auto octaves = [&](std::uint64_t stream, int count, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = count,
                                  .frequency = 1.0 / wavelength,
                                  .lattices = noise::OctaveLattices::RotatedFast},
                                 world);
    };
    Forms f;
    f.outline = octaves(1, 3, kLobeWavelength);
    f.warpX = octaves(2, 3, kWarpWavelength);
    f.warpZ = octaves(3, 3, kWarpWavelength);
    f.cliffs = octaves(4, 2, kCliffWavelength);
    f.faces = octaves(5, 1, kFaceWavelength);
    f.heights = octaves(6, 1, kHeightWavelength);
    f.outliers = octaves(7, 2, kOutlierWavelength);
    f.pans = octaves(8, 2, kPanWavelength);
    f.buttes = octaves(9, 2, kButteWavelength);
    f.edge = octaves(10, 2, kEdgeWavelength);
    return f;
}

Profile profileOf(const Forms& forms, const Site& s, double x, double z, double dx, double dz,
                  double above) {
    Place p = placeOf(forms, s, x, z, dx, dz);
    if (!(p.rho < 1.0)) {
        return {};
    }
    Profile out = softProfile(p, above);
    const double cliff = smooth(kTowardLow, kTowardHigh, p.toward) *
                         smooth(-kCliffEdge, kCliffEdge, fbm(forms.cliffs, x, z));
    if (cliff > 0.0) {
        // The cliff is laid in meters across the rim: the distance to it to first order,
        // (1 - rho) / |grad rho| (the lobes of the rim bend its line much faster than its radius
        // says), up along the gradient.
        const Place east = placeOf(forms, s, x + kRun, z, dx + kRun, dz);
        const Place south = placeOf(forms, s, x, z + kRun, dx, dz + kRun);
        const double gx = (east.rho - p.rho) / kRun;
        const double gz = (south.rho - p.rho) / kRun;
        const double g = std::max(dmath::hypot(gx, gz), 1.0e-9);
        Place across = p;
        across.radius = 1.0 / g;
        across.delta = (1.0 - p.rho) / g;
        across.upX = gx / g;
        across.upZ = gz / g;
        const Profile c = cliffProfile(forms, across, x, z, above);
        p.upX = across.upX;
        p.upZ = across.upZ;
        out.share += cliff * (c.share - out.share);
        out.slope += cliff * (c.slope - out.slope);
        out.onFloor += cliff * (c.onFloor - out.onFloor);
        out.room += cliff * (c.room - out.room);
    }
    out.upX = p.upX;
    out.upZ = p.upZ;
    return out;
}

Floor floorAt(const Forms& forms, double x, double z) {
    Floor f;
    f.pan = smooth(kPanLow, kPanHigh, fbm(forms.pans, x, z));
    if (f.pan < 1.0) {
        const double butte = smooth(kButteLow, kButteHigh, fbm(forms.buttes, x, z));
        if (butte > 0.0) {
            f.butte = (1.0 - f.pan) * butte *
                      (kButteMin + (kButteMax - kButteMin) *
                                       smooth(-0.4, 0.4, fbm(forms.heights, x + 5.0e5, z)));
        }
    }
    return f;
}

} // namespace terrain::depressions
