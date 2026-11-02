#include "coast_shape.hpp"

#include "coast_profile.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/coast.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace terrain::coast_shape {

namespace {

using coast_profile::Face;
using coast_profile::faceAt;
using coast_profile::kBenchFoot;
using coast_profile::kSmoothSlope;
using coast_profile::lerp;
using coast_profile::Profile;
using coast_profile::profileOf;
using coast_profile::Shore;
using coast_profile::shoreAt;
using coast_profile::smooth;
using coast_profile::unit;

// The coastal plain rises this many meters per meter from the shore, up to kCoastPlainTop,
// before the regional elevation takes over over GeographyParams::coastRamp.
constexpr double kCoastPlainRise = 0.002;
constexpr double kCoastPlainTop = 20.0;

// Streams and wavelengths of the noises of the shapes.
constexpr std::uint64_t kHeightStream = 0x33B1;
constexpr std::uint64_t kPlatformStream = 0x33B2;
constexpr std::uint64_t kFaceStream = 0x33B3;
constexpr std::uint64_t kStretchStream = 0x33B4;
constexpr std::uint64_t kEdgeStream = 0x33B5;
constexpr std::uint64_t kScreeStream = 0x33B6;
constexpr double kStretchWavelength = 30'000.0;
constexpr double kHeightWavelength = 5'000.0;
constexpr double kPlatformWavelength = 1'700.0;
constexpr double kFaceWavelength = 900.0;
constexpr double kEdgeWavelength = 350.0;
constexpr double kScreeWavelength = 1'200.0;

// The slope law of the faces (faceSlopeLimit): the height of a face in cells of kFaceCell meters
// (the LOD 3 of the tiles), and the steepest slope drawn without teeth at each height in cells,
// each twice the one before (measured, coast_patterns_test).
constexpr double kFaceCell = 8.0;
constexpr std::array<double, 4> kCleanCells{2.5, 5.0, 10.0, 20.0};
constexpr std::array<double, 4> kCleanSlopes{0.75, 1.25, 2.0, 3.0};
// The screes: where their noise passes its bounds (coast_profile.cpp: shoreAt), an apron up to
// kScreeShare of the height of the face, ending kScreeEnd of the way up its width, at most
// kScreeSlope steep and never more than kScreeSteep of the face, blended into it over kScreeBlend
// of its height.
constexpr double kScreeShare = 0.4;
constexpr double kScreeEnd = 0.45;
constexpr double kScreeSlope = 0.7;
constexpr double kScreeSteep = 0.5;
constexpr double kScreeBlend = 0.25;
// The screes never reach the water side of the platform: their toe stays kScreeKeep of its width
// from the water at least.
constexpr double kScreeKeep = 0.6;
// The hills and the vallons of the hinterland come onto the plateau over kPlateauRise meters
// beyond the top of the face.
constexpr double kPlateauRise = 250.0;
// The mountains plunging into the sea rise from the shore, steepest there, over kPlungeRamp
// meters of coast signal (about 5 km): read on the signal, smooth, not on the distance to the
// shore, whose rounding the heights of the mountains would show.
constexpr double kPlungeRamp = 15'000.0;
constexpr double kGlenRamp = 3'000.0;
// Around a rock of the reefs, the sea floor at most kRockFoot meters deep per meter from its foot,
// bare rock kRockMargin meters from it (no sand at its water).
constexpr double kRockFoot = 0.5;
constexpr double kRockMargin = 20.0;
// Where the shore kinds weigh kHeldShare or more of the weight of the shore and plunging kinds,
// the plunging kinds keep off their platform.
constexpr double kHeldShare = 0.5;
// The top of the shore (shoreTop) as the ground stands kTopReach meters inland: the faces of the
// shore kinds whole, the plunging kinds that far up their rise (kSignalPerMeter meters of coast
// signal per meter near the shore, as the inlets read it).
constexpr double kTopReach = 400.0;
constexpr double kSignalPerMeter = 3.0;
// Every kind gives way to the low coast (the ground and the factors of the land out of the band)
// over the last kInnerFade meters of signal of the band, the last kInnerSlack excepted.
constexpr double kInnerFade = 10'000.0;
constexpr double kInnerSlack = 1'000.0;

// 1 - (1 - t)^2 over [0, 1]: steepest at 0, flat at 1.
double easeOut(double t) {
    const double u = std::clamp(t, 0.0, 1.0);
    return u * (2.0 - u);
}

// The greater of a and b, rounded over k (polynomial smooth maximum).
double smoothMax(double a, double b, double k) {
    if (!(k > 0.0)) {
        return std::max(a, b);
    }
    const double h = std::max(k - std::abs(a - b), 0.0) / k;
    return std::max(a, b) + 0.25 * k * h * h;
}

// The lesser of a and b, rounded over k.
double smoothMin(double a, double b, double k) {
    return -smoothMax(-a, -b, k);
}

noise::FbmOctaves shapeNoise(std::uint64_t seed, std::uint64_t stream, double wavelength,
                             const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                             {.octaves = 3, .frequency = 1.0 / wavelength, .gain = 0.5}, world);
}


// The apron of screes at the foot of a face, at distance d from the water: rising as the square
// of the distance from its toe up to its top, kScreeEnd of the way up the face, at most
// kScreeSlope and kScreeSteep of the face steep there; smaller where the platform leaves no room
// (its toe never on the water side of the platform, which stays flat).
double screesAt(const Face& f, double patch, double d) {
    double top = patch * kScreeShare * f.height;
    const double slope =
        std::min(kScreeSlope, kScreeSteep * kSmoothSlope * f.height / f.width);
    if (!(top > 0.0) || !(slope > 0.0)) {
        return f.bench;
    }
    const double end = f.foot + kScreeEnd * f.width;
    double width = 2.0 * top / slope;
    const double toe = std::max(kBenchFoot, kScreeKeep * f.foot);
    if (end - width < toe) {
        const double room = std::max(0.0, end - toe);
        top *= (room / width) * (room / width);
        width = room;
    }
    if (!(width > 0.0)) {
        return f.bench;
    }
    const double u = std::clamp((d - (end - width)) / width, 0.0, 1.0);
    return f.bench + top * u * u;
}

// The factors of the land and the ground of a kind on land (coast > 0), before the inner edge of
// the band.
struct Land {
    double land = 0.0;
    double mountains = 0.0;
    double ground = 0.0;
    double up = 1.0; // the shore kinds: how far up their face, 0 on the platform, 1 on the top
};

// The rocky coasts, the cliffs, the calanques and the rias: the platform, the screes and the face
// up to a plateau as high as the height noises want, its hills and vallons coming onto it beyond
// the top of the face, its mountains later.
Land shoreOf(const Profile& k, const GeographySample& g, double coast, double d, const Shore& s,
             double old) {
    const double high = lerp(k.heightLow, k.heightHigh, s.heightAt);
    const double cap = static_cast<double>(k.landShare) *
                           std::max(0.0, static_cast<double>(g.land)) +
                       static_cast<double>(k.landTop);
    const Face f = faceAt(k, std::min(high, cap), d, s);
    const double face = f.bench + f.height * smooth(f.foot, f.foot + f.width, d);
    const double patch = static_cast<double>(k.screes) * s.screeAt;
    // Rounded where the screes meet the face, never more than they stand above the platform: the
    // platform before them keeps its height.
    const double screes = patch > 0.0 ? screesAt(f, patch, d) : f.bench;
    const double round = std::min(kScreeBlend * patch * f.height, screes - f.bench);
    const double ground = round > 0.0 ? smoothMax(face, screes, round) : std::max(face, screes);
    const double top = f.foot + f.width;
    const double land = smooth(top, top + kPlateauRise, d);
    return {land, land * smooth(0.0, k.hills, coast), std::max(old, ground),
            smooth(f.foot, top, d)};
}

// How far the plunging mountains have risen from the shore of the geography: steepest there, over
// kPlungeRamp of coast signal, or over kGlenRamp only in the reach of a fjord (Inlets::glen),
// whose own water they rise from (Inlets::rise): no low banks along it, no wall on the open sea
// at its mouth.
double rampOf(const Inlets& inlets) {
    const double ramp = easeOut(inlets.base / kPlungeRamp);
    return ramp + (easeOut(inlets.base / kGlenRamp) - ramp) * inlets.glen;
}

// The mountains and the fjords: the mountains and the regional elevation rise from the shore of
// the geography (rampOf), steepest there (no ledge at the water), and from the sides of the
// inlets (rise), held down by held (in [0, 1]) where they share the coast with cliffs.
Land plungeOf(const GeographySample& g, double coast, const Inlets& inlets, double held) {
    const double plain = std::min(kCoastPlainRise * coast, kCoastPlainTop);
    const double rise = inlets.rise * (rampOf(inlets) * held); // the order of before: same bits
    return {rise, rise, plain + (static_cast<double>(g.land) - plain) * rise};
}

bool isShore(CoastKind kind) {
    return kind == CoastKind::Rocky || kind == CoastKind::Cliff || kind == CoastKind::Calanque ||
           kind == CoastKind::Ria;
}

bool plunges(CoastKind kind) {
    return kind == CoastKind::Mountain || kind == CoastKind::Fjord;
}

// The land of the kinds blended by their weights: the shore kinds first; where they weigh more
// than the plunging kinds (from kHeldShare of the weight of both on), the plunging kinds rise only
// up the face of the shore kinds, leaving their platform flat; the low coast as without the
// coasts.
Land blendLand(const coast_kinds::Weights& weights, const GeographySample& g, double coast,
               double d, const Shore& s, double oldLand, double old, const Inlets& inlets) {
    Land sum;
    double shoreWeight = 0.0;
    double up = 0.0;
    double plungeWeight = 0.0;
    const auto add = [&sum](double w, const Land& l) {
        sum.land += w * l.land;
        sum.mountains += w * l.mountains;
        sum.ground += w * l.ground;
    };
    for (std::size_t k = 0; k < weights.size(); ++k) {
        const auto kind = static_cast<CoastKind>(k);
        const auto w = static_cast<double>(weights[k]);
        plungeWeight += plunges(kind) ? w : 0.0;
        if (isShore(kind) && w > 0.0) {
            const Land l = shoreOf(profileOf(kind), g, coast, d, s, old);
            add(w, l);
            shoreWeight += w;
            up += w * l.up;
        }
    }
    const double share = shoreWeight > 0.0 ? shoreWeight / (shoreWeight + plungeWeight) : 0.0;
    const double held =
        shoreWeight > 0.0 ? 1.0 - smooth(0.0, kHeldShare, share) * (1.0 - up / shoreWeight) : 1.0;
    for (std::size_t k = 0; k < weights.size(); ++k) {
        const auto kind = static_cast<CoastKind>(k);
        if (weights[k] > 0.0f && plunges(kind)) {
            add(static_cast<double>(weights[k]),
                plungeOf(g, coast, inlets, held));
        }
    }
    add(static_cast<double>(weights[static_cast<std::size_t>(CoastKind::Low)]),
        {oldLand, oldLand, old});
    return sum;
}

// The floor of a kind at sea (coast < 0): from the shelf at the shore, the share deepen of the way
// down to the shelf at the outer edge of the band within foot meters (steepest at the shore),
// never below it: the floor only goes down out to sea, and meets the shelf there (no ridge along
// the coast, no ledge).
double seaOf(const GeographyParams& p, CoastKind kind, const GeographySample& g, double coast,
             double d) {
    if (kind == CoastKind::Low) {
        return static_cast<double>(groundBase(p, g, coast));
    }
    const Profile& k = profileOf(kind);
    const double edge = shelf(p, -kCoastSeaBand);
    const double down = static_cast<double>(k.deepen) * easeOut(-d / k.foot);
    const double floor = edge + (shelf(p, coast) - edge) * (1.0 - down);
    return std::max(static_cast<double>(g.sea), floor);
}

} // namespace

double faceSlopeLimit(double height) {
    const double cells = height / kFaceCell;
    if (!(cells > kCleanCells.front())) {
        return kCleanSlopes.front();
    }
    for (std::size_t i = 1; i < kCleanCells.size(); ++i) {
        if (cells < kCleanCells[i]) {
            const double t = dmath::log2(cells / kCleanCells[i - 1]);
            return kCleanSlopes[i - 1] + (kCleanSlopes[i] - kCleanSlopes[i - 1]) * t;
        }
    }
    return kCleanSlopes.back();
}

bool inBand(const GeographyParams& p, double coast) {
    return coast > -kCoastSeaBand && coast < p.coastRamp;
}

float landFactor(const GeographyParams& p, double coast) {
    return noise::smoothstep(0.0f, static_cast<float>(p.coastRamp), static_cast<float>(coast));
}

double shelf(const GeographyParams& p, double coast) {
    const double d = -coast;
    const double depth = static_cast<double>(p.shelfDepth);
    return d <= p.shelfWidth ? depth * d / p.shelfWidth
                             : depth - (d - p.shelfWidth) * p.shelfSlope;
}

float groundBase(const GeographyParams& p, const GeographySample& g, double coast) {
    if (coast < 0.0) {
        return std::max(g.sea, static_cast<float>(shelf(p, coast)));
    }
    const double plain = std::min(kCoastPlainRise * coast, kCoastPlainTop);
    const double t = landFactor(p, coast);
    return static_cast<float>(plain + (static_cast<double>(g.land) - plain) * t);
}

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    return {shapeNoise(seed, kStretchStream, kStretchWavelength, world),
            shapeNoise(seed, kHeightStream, kHeightWavelength, world),
            shapeNoise(seed, kPlatformStream, kPlatformWavelength, world),
            shapeNoise(seed, kFaceStream, kFaceWavelength, world),
            shapeNoise(seed, kEdgeStream, kEdgeWavelength, world),
            shapeNoise(seed, kScreeStream, kScreeWavelength, world)};
}

Ground shapeAt(const GeographyParams& p, const Noises& noises, double x, double z,
               const GeographySample& g, double coast, double distance,
               const coast_kinds::Weights& weights, const Inlets& inlets) {
    if (coast < 0.0) {
        double ground = 0.0;
        for (std::size_t k = 0; k < weights.size(); ++k) {
            if (weights[k] > 0.0f) {
                ground += static_cast<double>(weights[k]) *
                          seaOf(p, static_cast<CoastKind>(k), g, coast, distance);
            }
        }
        // In the inlets the plunging mountains go on down under the water, to their floor.
        double plunge = 0.0;
        for (const CoastKind kind : {CoastKind::Mountain, CoastKind::Fjord}) {
            plunge += static_cast<double>(weights[static_cast<std::size_t>(kind)]);
        }
        const double mirror = plunge * std::max(0.0, static_cast<double>(g.land)) *
                              rampOf(inlets) * inlets.sink;
        // Rounded where the mountains meet the floor (a quarter of its depth).
        const double floor =
            std::min(inlets.floor, -smoothMin(mirror, inlets.deep, 0.25 * inlets.deep));
        // Toward a rock of the reefs the floor rises to its foot, at most kRockFoot steep; bare
        // rock up to kRockMargin meters from it.
        const double foot = kRockFoot * inlets.islet;
        return {0.0f, 0.0f, static_cast<float>(std::max(std::min(ground, floor), foot)), 1.0f,
                0.0f, inlets.islet > -kRockMargin ? 1.0f : 0.0f};
    }
    if (inlets.base < 0.0) {
        // A rock of the reefs out in the sea of the geography: its dome alone (no face of the
        // coast, no relief of the land on it).
        return {0.0f, 0.0f, static_cast<float>(inlets.dome), 1.0f, 0.0f, 1.0f};
    }
    bool shores = false;
    for (const CoastKind kind :
         {CoastKind::Rocky, CoastKind::Cliff, CoastKind::Calanque, CoastKind::Ria}) {
        shores = shores || weights[static_cast<std::size_t>(kind)] > 0.0f;
    }
    const Shore shore = shores ? shoreAt(noises, x, z) : Shore{};
    const double oldLand = static_cast<double>(landFactor(p, coast));
    const double old = static_cast<double>(groundBase(p, g, coast));
    Land sum = blendLand(weights, g, coast, distance, shore, oldLand, old, inlets);
    sum.ground = std::max(sum.ground, inlets.dome);
    // At the inner edge of the band, the world without the coasts (the inlets never reach it).
    const double inner =
        smooth(p.coastRamp - kInnerFade, p.coastRamp - kInnerSlack, inlets.base);
    const double outLand = static_cast<double>(landFactor(p, inlets.base));
    const double out = static_cast<double>(groundBase(p, g, inlets.base));
    const auto land = static_cast<float>(sum.land + (outLand - sum.land) * inner);
    const float low = weights[static_cast<std::size_t>(CoastKind::Low)];
    return {land, static_cast<float>(sum.mountains + (outLand - sum.mountains) * inner),
            static_cast<float>(sum.ground + (out - sum.ground) * inner),
            low + (1.0f - low) * land, static_cast<float>((1.0 - low) * (1.0 - inner))};
}

double shoreTop(const Noises& noises, double x, double z, const GeographySample& g,
                const coast_kinds::Weights& weights) {
    const Shore s = shoreAt(noises, x, z);
    const double land = std::max(0.0, static_cast<double>(g.land));
    const double plunge = land * easeOut(kSignalPerMeter * kTopReach / kPlungeRamp);
    double top = 0.0;
    for (std::size_t k = 0; k < weights.size(); ++k) {
        const auto kind = static_cast<CoastKind>(k);
        const auto w = static_cast<double>(weights[k]);
        if (isShore(kind) && w > 0.0) {
            const Profile& p = profileOf(kind);
            const double high = lerp(p.heightLow, p.heightHigh, s.heightAt);
            const double cap =
                static_cast<double>(p.landShare) * land + static_cast<double>(p.landTop);
            top += w * std::min(high, cap);
        } else if (plunges(kind)) {
            top += w * plunge;
        }
    }
    return top;
}

} // namespace terrain::coast_shape
