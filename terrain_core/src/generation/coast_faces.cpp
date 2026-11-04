#include "coast_faces.hpp"

#include "coast_profile.hpp"
#include "coast_reefs.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::coast_faces {

namespace {

using coast_profile::smooth;

// No face beyond kFarthest meters from the shore on either side (wider than the widest run: the
// widest platform and face); within kRockSide meters of a rock of the reefs, its own face alone,
// the faces of the shore fading out over the kRockFade meters before (no ledge of a face over the
// rock, no face cut along the coast).
constexpr double kFarthest = 600.0;
// The edges of the faces of the coasts rounded over kFaceRoundTenths tenths of a meter (the walls:
// 3 m, a cushion along their top; rock_faces.cpp).
constexpr std::uint8_t kFaceRoundTenths = 10;
constexpr double kRockSide = 30.0;
constexpr double kRockFade = 10.0;
// The faced kinds (the rocky coasts, the cliffs and the calanques) lay a face from kFacedLow of
// the weights of the kinds of coast on, fully from kFacedHigh.
constexpr double kFacedLow = 0.3;
constexpr double kFacedHigh = 0.7;
// The heaviest of the faced kinds lays its face from kDominantLow of their weights on, fully from
// kDominantHigh.
constexpr double kDominantLow = 0.55;
constexpr double kDominantHigh = 0.75;
// The notch, where the foot of the face stands kNotchLow to kNotchHigh meters over the sea level:
// kNotchDeepLow to kNotchDeepHigh meters deep and kNotchHeightLow to kNotchHeightHigh high, at
// most kNotchShare of the face and kLipShape times as high as deep at least (as the undercuts of
// the walls), open where the noise of the overhangs passes kNotchOpen to kNotchFull. The tiles
// draw no overhang reaching less than 1.5 to 3 of their cells nor thinner than 3 to 6 of them
// (rock_faces.cpp): a notch 3 m deep and 6 m high is drawn whole at LOD 0.
constexpr double kNotchLow = -1.0;
constexpr double kNotchHigh = 2.5;
constexpr double kNotchDeepLow = 3.0;
constexpr double kNotchDeepHigh = 4.0;
constexpr double kNotchHeightLow = 6.0;
constexpr double kNotchHeightHigh = 8.0;
constexpr double kNotchShare = 0.3;
constexpr double kLipShape = 2.0;
constexpr double kNotchOpen = -0.3;
constexpr double kNotchFull = 0.2;
// The caps of the cliffs and the calanques: kCapShare of the face, kCapMost meters at most, where
// the noise of the overhangs passes kCapOpen to kCapFull, kLipShape times their reach thick and
// kCapDepth meters at least (drawn whole at LOD 0 from 3 m).
constexpr double kCapShare = 0.1;
constexpr double kCapMost = 4.0;
constexpr double kCapOpen = 0.3;
constexpr double kCapFull = 0.8;
constexpr double kCapDepth = 6.0;
// The beds of the strata: kBedGrain meters on the cliffs and the calanques, kRockyGrain on the
// rocky coasts, shifting by up to kBedShift meters along the faces.
constexpr double kBedGrain = 0.35;
constexpr double kRockyGrain = 0.2;
constexpr double kBedShift = 3.0;
// The overhangs and the grain stay within kWithinRun of the run (as the faces of the walls).
constexpr double kWithinRun = 0.45;
// The lower walls of the fjords: kFjordWallLow to kFjordWallHigh meters over the water.
constexpr double kFjordWallLow = 30.0;
constexpr double kFjordWallHigh = 150.0;
// The columns of the bank of a fjord within kFjordReach times the height of its wall from the
// water stand on its top, fading out from kFjordFade of that reach; its run kFjordRun times its
// height each side of the water (the half of it under the sea), its foot at the sea level.
constexpr double kFjordReach = 1.5;
constexpr double kFjordFade = 0.67;
constexpr double kFjordRun = 0.5;
// Only on a steep bank: the ground of the coast rising from the water kFjordSteepLow to
// kFjordSteepHigh meters per meter of the distance to the shore at least (at kFjordSteepFrom meters
// of it at least: about 2 on the walls of the fjords, 0.2 on their islands), the face faded in
// between.
constexpr double kFjordSteepLow = 0.5;
constexpr double kFjordSteepHigh = 1.0;
constexpr double kFjordSteepFrom = 0.1;
// The columns of the water before the wall within kFjordFront meters of it hold its face.
constexpr double kFjordFront = 16.0;

// The heaviest of the faced kinds, and their share of the weights of the kinds.
struct Faced {
    CoastKind kind = CoastKind::Cliff;
    double share = 0.0;
    double dominance = 0.0; // the share of the heaviest among the faced kinds
};

Faced facedOf(const coast_kinds::Weights& weights) {
    Faced out;
    double heaviest = -1.0;
    double total = 0.0;
    for (std::size_t k = 0; k < weights.size(); ++k) {
        total += static_cast<double>(weights[k]);
    }
    for (const CoastKind kind : {CoastKind::Rocky, CoastKind::Cliff, CoastKind::Calanque}) {
        const auto w = static_cast<double>(weights[static_cast<std::size_t>(kind)]);
        out.share += w;
        if (w > heaviest) {
            heaviest = w;
            out.kind = kind;
        }
    }
    out.dominance = out.share > 0.0 ? heaviest / out.share : 0.0;
    out.share = total > 0.0 ? out.share / total : 0.0;
    return out;
}

// The ground of the coast at the point as it would stand distance meters from the shore.
double groundAt(const GeographyParams& p, const Point& point, const GeographySample& g,
                const coast_kinds::Weights& weights, const coast_shape::Inlets& inlets,
                const coast_profile::Shore& shore, double distance) {
    // A column of the sea of the geography read at a distance on the land is read as the land
    // there (shapeAt would give it the dome of a rock of the reefs out at sea: none).
    coast_shape::Inlets read = inlets;
    const double coast = distance * point.gradient;
    if (coast > 0.0 && read.base < 0.0) {
        read.base = coast;
    }
    return static_cast<double>(
        coast_shape::shapeAt(p, g, coast, distance, weights, read, shore).ground);
}

// The notch, the cap and the grain of a face of kind over its run, its foot at foot meters over the
// sea level.
void featuresOf(const wall_faces::Grain& grain, const Point& point, CoastKind kind, double foot,
                bool plunges, RockFace& face) {
    const double run = face.run;
    const double height = static_cast<double>(face.top) - face.foot;
    const bool strata = kind == CoastKind::Cliff || kind == CoastKind::Calanque;
    face.grain = static_cast<float>(std::min(strata ? kBedGrain : kRockyGrain, 0.1 * run));
    face.grainShift =
        static_cast<float>(kBedShift * static_cast<double>(grain.shift.fbm2D(point.x, point.z)));
    const double room = std::max(0.0, kWithinRun * run - 2.0 * static_cast<double>(face.grain));
    const double over = static_cast<double>(grain.overhangs.fbm2D(point.x, point.z));
    if (!plunges && foot > kNotchLow && foot < kNotchHigh) {
        const double open = smooth(kNotchOpen, kNotchFull, -over);
        const double t = 0.5 + 0.5 * over;
        const double high = std::min(kNotchShare * height,
                                     kNotchHeightLow + (kNotchHeightHigh - kNotchHeightLow) * t);
        const double reach = open * (kNotchDeepLow + (kNotchDeepHigh - kNotchDeepLow) * t);
        const double deep = std::min({room, high / kLipShape, reach});
        face.undercut = static_cast<float>(deep);
        face.undercutHeight =
            static_cast<float>(deep > 0.0 ? std::max(high, kLipShape * deep) : 0.0);
    }
    if (strata) {
        const double reach = std::min(kCapShare * height, kCapMost);
        face.cap = static_cast<float>(std::min(room, reach * smooth(kCapOpen, kCapFull, over)));
        face.capDepth = static_cast<float>(std::min(
            height, std::max(kCapDepth, kLipShape * static_cast<double>(face.cap))));
    }
    // No joint (the check in Unreal of 1b77f95: a straight groove opening over a stretch of the
    // face, alone, read as a door): the beds draw its rock (rockBands).
    face.joint = 0.0f;
    face.roundTenths = kFaceRoundTenths;
}

// What faceAt reads, together.
struct Context {
    const GeographyParams& p;
    const wall_faces::Grain& grain;
    const Point& point;
    const GeographySample& g;
    const coast_kinds::Weights& weights;
    const coast_shape::Inlets& inlets;
    const coast_profile::Shore& shore;
};

double groundOf(const Context& c, double distance) {
    return groundAt(c.p, c.point, c.g, c.weights, c.inlets, c.shore, distance);
}

// The face of a run at the point, of kind (its grain and overhangs) and of weight share.
Report faceOfRun(const Context& c, const coast_profile::Run& run, double share, CoastKind kind) {
    Report out;
    const double d = c.point.distance;
    const double here = groundOf(c, d);
    const double foot = groundOf(c, run.a);
    const double top = groundOf(c, run.b);
    RockFace& face = out.face;
    face.foot = static_cast<float>(foot - here);
    face.top = static_cast<float>(std::max(foot, top) - here);
    face.distance = static_cast<float>(run.m - d);
    face.run = static_cast<float>(run.b - run.a);
    face.weight = static_cast<float>(share);
    face.airX = static_cast<float>(c.point.airX);
    face.airZ = static_cast<float>(c.point.airZ);
    featuresOf(c.grain, c.point, kind, foot, run.plunges, face);
    out.faced = true;
    out.plunges = run.plunges;
    out.alongShore = true;
    return out;
}

// The face of the rocky coasts, the cliffs and the calanques (the heaviest of them: kind).
Report shoreFaceAt(const Context& c, CoastKind kind, double share) {
    const double d = c.point.distance;
    const coast_profile::Profile& k = coast_profile::profileOf(kind);
    const coast_profile::Shore& s = c.shore;
    const coast_profile::Face f = coast_profile::wallFaceAt(
        k, coast_profile::faceTop(k, s, static_cast<double>(c.g.land)), std::max(d, 0.0), s);
    const coast_profile::Run run = coast_profile::runOf(f, kind == CoastKind::Calanque);
    if (d < run.a || d > run.b) {
        return {};
    }
    return faceOfRun(c, run, share, kind);
}

// The share of the fjords among the kinds of coast.
double fjordShareOf(const coast_kinds::Weights& weights) {
    double total = 0.0;
    for (const float w : weights) {
        total += static_cast<double>(w);
    }
    const auto fjord = static_cast<double>(weights[static_cast<std::size_t>(CoastKind::Fjord)]);
    return total > 0.0 ? fjord / total : 0.0;
}

// The lower wall of a fjord, standing at the shore: kFjordWallLow to kFjordWallHigh meters high
// by the noise of the heights of the shore. The ground of the fjords follows their inlets, not the
// distance to the shore: each column of the bank lower than the wall, within kFjordReach times the
// height of the wall from the water, stands as high as it (the ledge of its top), fading out over
// the last third of that reach; the sea before the wall keeps its floor.
Report fjordFrontAt(const Context& c, double share, double wall, double here);

Report fjordFaceAt(const Context& c, double share) {
    const double d = c.point.distance;
    const coast_profile::Shore& s = c.shore;
    const double wall = kFjordWallLow + (kFjordWallHigh - kFjordWallLow) * s.heightAt;
    const double reach = kFjordReach * wall;
    const double here = groundOf(c, d);
    if (!(d > 0.0)) {
        return fjordFrontAt(c, share, wall, here);
    }
    if (!(here < wall) || !(d < reach)) {
        return {};
    }
    // A bank rising gently from the water (an island, a gentle shore) keeps its slopes: the step of
    // the wall would stand over it as a ring of rock with a hollow behind it.
    const double steep = here / std::max(d, kFjordSteepFrom);
    if (!(steep > kFjordSteepLow)) {
        return {};
    }
    Report out;
    RockFace& face = out.face;
    face.foot = static_cast<float>(-here);
    face.top = static_cast<float>(wall - here);
    face.distance = static_cast<float>(-d);
    face.run = static_cast<float>(2.0 * std::max(kFjordRun * wall, d + 1.0));
    face.weight = static_cast<float>(share * (1.0 - smooth(kFjordFade * reach, reach, d)) *
                                     smooth(kFjordSteepLow, kFjordSteepHigh, steep));
    face.airX = static_cast<float>(c.point.airX);
    face.airZ = static_cast<float>(c.point.airZ);
    featuresOf(c.grain, c.point, CoastKind::Fjord, 0.0, true, face);
    out.faced = true;
    out.plunges = true;
    out.alongShore = true;
    return out;
}

// The face of the lower wall of a fjord on a column of its water before it, kFjordFront meters of
// it at most: its foot the floor there (no air under the floor of the sea), its top that of the
// wall, so that the density crosses the plane of the face as the distance to it (the columns of
// the water without the face would turn from the face to the air of the whole height of the wall
// from one column to the next: a staircase of the lattice).
Report fjordFrontAt(const Context& c, double share, double wall, double here) {
    const double d = c.point.distance;
    if (!(d > -kFjordRun * wall)) {
        return {};
    }
    Report out;
    RockFace& face = out.face;
    face.top = static_cast<float>(wall - here);
    face.distance = static_cast<float>(-d);
    face.run = static_cast<float>(2.0 * std::max(kFjordRun * wall, 1.0 - d));
    face.weight = static_cast<float>(share);
    face.airX = static_cast<float>(c.point.airX);
    face.airZ = static_cast<float>(c.point.airZ);
    featuresOf(c.grain, c.point, CoastKind::Fjord, 0.0, true, face);
    out.faced = true;
    out.plunges = true;
    out.alongShore = true;
    out.front = static_cast<float>(kFjordFront);
    return out;
}

// The spire of the rock of the reefs whose reach holds the point (a block or a stack), the point
// from its axis (coast_reefs::spireOf); none (radius 0) out of every reach.
RockSpire spireAt(const Context& c) {
    const coast_shape::Inlets& in = c.inlets;
    RockSpire out;
    if (!(in.spireSize > 0.0)) {
        return out;
    }
    out.dx = static_cast<float>(noise::wrapCanonical(c.point.x - in.spireX, c.grain.world.x));
    out.dz = static_cast<float>(noise::wrapCanonical(c.point.z - in.spireZ, c.grain.world.z));
    out.radius = static_cast<float>(in.spireSize);
    out.height = static_cast<float>(in.spireHeight);
    out.seed = coast_reefs::spireSeed(in.spireX, in.spireZ);
    return out;
}

} // namespace

Report faceAt(const GeographyParams& p, const wall_faces::Grain& grain, const Point& point,
              const GeographySample& g, const coast_kinds::Weights& weights,
              const coast_shape::Inlets& inlets, const coast_profile::Shore& shore) {
    const Context c{p, grain, point, g, weights, inlets, shore};
    // The rocks of the reefs lay no face within kRockSide meters of them: their spires, over
    // every point within their reach.
    if (inlets.islet > -kRockSide) {
        Report out;
        out.spire = spireAt(c);
        return out;
    }
    if (!(std::abs(point.distance) < kFarthest)) {
        return {};
    }
    const double clear = 1.0 - smooth(-kRockSide - kRockFade, -kRockSide, inlets.islet);
    const Faced faced = facedOf(weights);
    const double fjords = fjordShareOf(weights);
    if (fjords > faced.share) {
        const double share = clear * smooth(kFacedLow, kFacedHigh, fjords);
        return share > 0.0 ? fjordFaceAt(c, share) : Report{};
    }
    // Where two of the faced kinds share the coast, their faces (standing at their own places)
    // fade out between them: never a jump of the face along the coast.
    const double share = clear * smooth(kFacedLow, kFacedHigh, faced.share) *
                         smooth(kDominantLow, kDominantHigh, faced.dominance);
    return share > 0.0 ? shoreFaceAt(c, faced.kind, share) : Report{};
}

bool plungeRock(const Point& point, const GeographySample& g, const coast_kinds::Weights& weights,
                const coast_profile::Shore& shore) {
    const double d = point.distance;
    if (!(d < 0.0) || !(d > -kFarthest)) {
        return false;
    }
    const Faced faced = facedOf(weights);
    if (faced.kind != CoastKind::Calanque || !(faced.share > kFacedLow) ||
        !(faced.dominance > kDominantLow)) {
        return false;
    }
    const coast_profile::Profile& k = coast_profile::profileOf(CoastKind::Calanque);
    const coast_profile::Face f = coast_profile::wallFaceAt(
        k, coast_profile::faceTop(k, shore, static_cast<double>(g.land)), 0.0, shore);
    return d > coast_profile::runOf(f, true).a;
}

// A bank rising kBankLow meters per meter over the width of the ramp of a level keeps more of its
// height, from kBankHigh on all of it: a wall (the sea at its foot bare rock, barePlunge: the
// ramp would draw a shelf at the sea level before it, and hide it from that rule).
constexpr double kBankLow = 0.6;
constexpr double kBankHigh = 0.9;

double shoreRampOf(double pitch) {
    return 2.0 * kSeenRamp * pitch;
}

float seenShore(float relief, float shore, double pitch) {
    if (!(shore >= 0.0f) || !(relief > 0.0f)) {
        return relief;
    }
    const double ramp = shoreRampOf(pitch);
    const auto h = static_cast<double>(relief);
    const auto d = static_cast<double>(shore);
    const double wall = smooth(kBankLow, kBankHigh, h / std::max(d, ramp));
    return static_cast<float>(h * (wall + (1.0 - wall) * smooth(0.0, ramp, d)));
}

Seen seenOf(const RockFace& face, bool plunges, float relief, double pitch) {
    const double weight =
        std::min(1.0, static_cast<double>(drawnWeight(face, RockDetail{kSeenCell})));
    const double behind = -static_cast<double>(face.distance);
    const double run = static_cast<double>(face.run);
    // The sea before a face that plunges keeps the floor of its relief.
    if (!(weight > 0.0) || (plunges && !(behind > 0.0))) {
        return {relief, false};
    }
    double low = static_cast<double>(face.foot);
    double from = 0.0;
    double to = 0.0;
    if (plunges) {
        low = 0.0;
        to = std::min(2.0 * kSeenRamp * pitch, 0.5 * run);
    } else {
        to = std::min(kSeenRamp * pitch, 0.25 * run);
        from = -to;
    }
    const double ledge = low + smooth(from, to, behind) * (static_cast<double>(face.top) - low);
    const double height = static_cast<double>(relief) + weight * (ledge - relief);
    const bool rock = weight > 0.5 && behind > from - pitch && behind < to + pitch;
    return {static_cast<float>(height), rock};
}

} // namespace terrain::coast_faces
