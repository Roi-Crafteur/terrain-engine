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
constexpr double kRockSide = 30.0;
constexpr double kRockFade = 10.0;
// The rocks: stacks from kStackLeast meters high; the rocks of the reefs upright toward their
// phase, their slope kept beyond kRockSideOpen of the cosine of the angle from it. Where two rocks
// meet, the face of the one the point lies furthest into fades out over the last kRockMeet meters
// before the other one does (coast_inlets::Effect::nextIslet): no face cut off at their meeting.
constexpr double kStackLeast = 20.0;
constexpr double kRockSideOpen = 0.3;
constexpr double kRockMeet = 4.0;
constexpr double kPi = 3.14159265358979323846;
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
// The joints recede the face by kJointRun of its run at most; the overhangs and the grain stay
// within kWithinRun of the run (as the faces of the walls).
constexpr double kJointRun = 0.1;
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
double groundAt(const GeographyParams& p, const coast_shape::Noises& noises, const Point& point,
                const GeographySample& g, const coast_kinds::Weights& weights,
                const coast_shape::Inlets& inlets, double distance) {
    return static_cast<double>(coast_shape::shapeAt(p, noises, point.x, point.z, g,
                                                    distance * point.gradient, distance, weights,
                                                    inlets)
                                   .ground);
}

// The notch, the cap, the grain and the joint of a face of kind over its run, its foot at foot
// meters over the sea level.
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
    const wall_faces::Joint joint = wall_faces::jointAt(grain, point.x, point.z);
    face.joint = static_cast<float>(
        std::min({joint.depth, kJointRun * run,
                  std::max(0.0, room - static_cast<double>(face.undercut))}));
    face.jointWidth = static_cast<float>(joint.width);
    face.jointSeed = static_cast<float>(joint.seed);
}

// What faceAt reads, together.
struct Context {
    const GeographyParams& p;
    const coast_shape::Noises& noises;
    const wall_faces::Grain& grain;
    const Point& point;
    const GeographySample& g;
    const coast_kinds::Weights& weights;
    const coast_shape::Inlets& inlets;
};

double groundOf(const Context& c, double distance) {
    return groundAt(c.p, c.noises, c.point, c.g, c.weights, c.inlets, distance);
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
    return out;
}

// The face of the rocky coasts, the cliffs and the calanques (the heaviest of them: kind).
Report shoreFaceAt(const Context& c, CoastKind kind, double share) {
    const double d = c.point.distance;
    const coast_profile::Profile& k = coast_profile::profileOf(kind);
    const coast_profile::Shore s = coast_profile::shoreAt(c.noises, c.point.x, c.point.z);
    const coast_profile::Face f = coast_profile::faceAt(
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
Report fjordFaceAt(const Context& c, double share) {
    const double d = c.point.distance;
    if (!(d > 0.0)) {
        return {};
    }
    const coast_profile::Shore s = coast_profile::shoreAt(c.noises, c.point.x, c.point.z);
    const double wall = kFjordWallLow + (kFjordWallHigh - kFjordWallLow) * s.heightAt;
    const double reach = kFjordReach * wall;
    const double here = groundOf(c, d);
    if (!(here < wall) || !(d < reach)) {
        return {};
    }
    Report out;
    RockFace& face = out.face;
    face.foot = static_cast<float>(-here);
    face.top = static_cast<float>(wall - here);
    face.distance = static_cast<float>(-d);
    face.run = static_cast<float>(2.0 * std::max(kFjordRun * wall, d + 1.0));
    face.weight = static_cast<float>(share * (1.0 - smooth(kFjordFade * reach, reach, d)));
    face.airX = static_cast<float>(c.point.airX);
    face.airZ = static_cast<float>(c.point.airZ);
    featuresOf(c.grain, c.point, CoastKind::Fjord, 0.0, true, face);
    out.faced = true;
    return out;
}

// The face of a rock of the reefs or of a stack: standing at its water (its radius toward the
// point, its lobes), its run from kRockTop of that radius inside (where its dome begins) to as far
// outside; the shoulder of its dome behind the face (the dome over the inner end of the run, its
// rough relief: the dome rises on from there), the sea level before it, the floor under the sea as
// it is (the columns of the rock alone hold the face: those out of it keep their relief).
// The rocks of the reefs stand upright toward one side only (their phase: the other side keeps
// its slope), the stacks (kStackLeast meters high at least) all around.
Report rockFaceAt(const Context& c) {
    const coast_shape::Inlets& in = c.inlets;
    if (!(in.rockRadius > 0.0)) {
        return {};
    }
    const double dx = noise::wrapCanonical(c.point.x - in.rockX, c.grain.world.x);
    const double dz = noise::wrapCanonical(c.point.z - in.rockZ, c.grain.world.z);
    const double r = dmath::hypot(dx, dz);
    const double radius = in.rockRadius;
    const double reach = (1.0 - coast_reefs::kRockTop) * radius;
    if (!(r < radius + reach) || !(r > radius - reach)) {
        return {};
    }
    double weight = 1.0;
    if (in.rockHeight < kStackLeast) {
        const double turns = r > 0.0 ? dmath::atan2(dz, dx) / (2.0 * kPi) : 0.0;
        weight = smooth(-kRockSideOpen, kRockSideOpen,
                        dmath::sinTurns(turns - in.rockPhase + 0.25));
    }
    weight *= smooth(0.0, kRockMeet, in.islet - in.nextIslet);
    if (!(weight > 0.0)) {
        return {};
    }
    const double here = groundOf(c, c.point.distance);
    const bool inside = r < radius;
    Report out;
    RockFace& face = out.face;
    face.foot = static_cast<float>(inside ? -here : 0.0);
    const double shoulder = in.rockTop * coast_reefs::domeOf(1.0 - coast_reefs::kRockTop);
    face.top = static_cast<float>(inside ? std::max(0.0, shoulder - here) : 0.0);
    face.distance = static_cast<float>(r - radius);
    face.run = static_cast<float>(2.0 * reach);
    face.weight = static_cast<float>(weight);
    face.airX = static_cast<float>(r > 0.0 ? dx / r : 1.0);
    face.airZ = static_cast<float>(r > 0.0 ? dz / r : 0.0);
    featuresOf(c.grain, c.point, CoastKind::Rocky, 0.0, true, face);
    out.faced = true;
    return out;
}

} // namespace

Report faceAt(const GeographyParams& p, const coast_shape::Noises& noises,
              const wall_faces::Grain& grain, const Point& point, const GeographySample& g,
              const coast_kinds::Weights& weights, const coast_shape::Inlets& inlets) {
    const Context c{p, noises, grain, point, g, weights, inlets};
    // The rocks of the reefs lay their own faces, within kRockSide meters of them.
    if (inlets.islet > -kRockSide) {
        return rockFaceAt(c);
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

bool plungeRock(const coast_shape::Noises& noises, const Point& point, const GeographySample& g,
                const coast_kinds::Weights& weights) {
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
    const coast_profile::Shore s = coast_profile::shoreAt(noises, point.x, point.z);
    const coast_profile::Face f =
        coast_profile::faceAt(k, coast_profile::faceTop(k, s, static_cast<double>(g.land)), 0.0, s);
    return d > coast_profile::runOf(f, true).a;
}

float seenHeight(const RockFace& face, float relief, const RockDetail& detail) {
    const double weight = std::min(1.0, static_cast<double>(drawnWeight(face, detail)));
    // In front of the face under the sea the floor keeps its relief: the horizon draws the sea
    // flat there and colors it by that floor, interpolated toward the top of the face behind; a
    // floor as deep as the foot of the face would paint the sea up the lower part of its wall.
    if (!(weight > 0.0) || (face.distance >= 0.0f && relief < 0.0f)) {
        return relief;
    }
    // The step over a cell of the detail across the face (half of it on each side): the samples
    // next to the face stand between its ledges, so that a face oblique to the grid keeps its
    // line, not a staircase of the samples.
    const double half = 0.5 * static_cast<double>(detail.cell);
    const double behind = smooth(-half, half, -static_cast<double>(face.distance));
    const double ledge = static_cast<double>(face.foot) +
                         behind * (static_cast<double>(face.top) - face.foot);
    return static_cast<float>(static_cast<double>(relief) +
                              weight * (ledge - static_cast<double>(relief)));
}

} // namespace terrain::coast_faces
