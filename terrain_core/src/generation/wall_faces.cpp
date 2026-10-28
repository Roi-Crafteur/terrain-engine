// The faces of the walls of the mountains in three dimensions (wall_faces.hpp).
#include "wall_faces.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace terrain::wall_faces {

namespace {

using mountain_walls::StrataPass;

// The passes whose risers stand as faces: the great bands of the strata and their rock steps, the
// joints of both scales of the crystalline rock.
constexpr std::array<std::size_t, 4> kFacePasses = {0, 1, 2, 3};
// The faces of the walls fade in where their mask times the hardness of their layer runs from
// kFaintLow (the slope of the relief) to kFaintHigh (vertical): the mask of the strata for their
// steps, which rockTop lays at a share of it only.
constexpr double kFaintLow = 0.3;
constexpr double kFaintHigh = 0.6;
constexpr double kWholeCell = 0.95;
// The cap of a face juts out up to kCapShare of its height, kCapMost meters at most, over the top
// kCapDepthLow to kCapDepthLow + kCapDepthSpread of it; its undercut recedes up to kCutShare of its
// height, kCutMost meters at most, up to kCutHeightLow to kCutHeightLow + kCutHeightSpread of it.
// Both stay within kWithinRun of the run of the face.
constexpr double kCapShare = 0.2;
constexpr double kCapMost = 8.0;
constexpr double kCapDepthLow = 0.12;
constexpr double kCapDepthSpread = 0.18;
constexpr double kCutShare = 0.15;
constexpr double kCutMost = 6.0;
constexpr double kCutHeightLow = 0.1;
constexpr double kCutHeightSpread = 0.2;
constexpr double kWithinRun = 0.45;
// A face has a cap where the draw of its layer and the noise along it (kCapNoise of it) pass
// kCapFrom to kCapFull, an undercut where they pass kCutFrom to kCutFull (the noise reversed: the
// rock under a cap is often whole).
constexpr double kCapNoise = 0.45;
// The noise along the walls comes and goes over about kOverhangWavelength meters.
constexpr double kOverhangWavelength = 90.0;
constexpr double kCapFrom = 0.45;
constexpr double kCapFull = 0.8;
constexpr double kCutFrom = 0.5;
constexpr double kCutFull = 0.85;
// The grain of a face stands out up to kGrainShare of its height, kGrainMost meters at most, and
// kGrainRun of its run; its bands shift by up to kShiftRange meters along the faces, by a noise of
// kShiftWavelength meters.
constexpr double kGrainShare = 0.04;
constexpr double kGrainMost = 1.5;
constexpr double kGrainRun = 0.12;
constexpr double kShiftRange = 12.0;
constexpr double kShiftWavelength = 45.0;
// The slope of the ground under a pass is read over kDelta meters of the ground of the walls, at
// least kLeastSlope meters per meter.
constexpr double kDelta = 0.5;
constexpr double kLeastSlope = 0.05;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The top of a pass over ground (as rockTop lays it), lift meters along its planes.
double passTop(const StrataPass& p, double lift, double ground) {
    if (!(p.weight > 0.0)) {
        return ground;
    }
    const double top =
        mountain_walls::strataTop(ground + lift, p.slope, p.band, p.offset, p.rim, p.run,
                                  p.bedding) -
        lift;
    return ground + p.weight * (top - ground);
}

// The ground under pass last of a column over ground: the ground the first pass lays its layers
// on, then the top each one leaves to the next.
double groundUnder(const mountain_walls::WallPasses& w, double ground, std::size_t last) {
    for (std::size_t i = 0; i < last; ++i) {
        ground = passTop(w.passes[i], w.lift[i], ground);
    }
    return ground;
}

// The riser of a pass a column lies within: its layer, where it starts and ends in the ground of
// the pass (lifted) and its middle over the ground of the column.
struct Riser {
    std::int64_t k = 0;
    mountain_walls::LayerFace layer;
    double start = 0.0;
    double end = 0.0;
    double level = 0.0;
};

// The riser of the layer holding ground (lifted), if ground lies within it.
bool riserAt(const StrataPass& p, double ground, Riser& out) {
    out.k = mountain_walls::layerOf(p, ground);
    out.layer = mountain_walls::layerFace(p, out.k);
    const double recede = p.rim * std::max(p.slope, kLeastSlope) + p.offset;
    out.start = out.layer.start * p.band + recede;
    out.end = out.layer.end * p.band + recede;
    out.level = 0.5 * (out.start + out.end) - ground;
    return out.end > out.start && std::abs(out.level) <= 0.5 * (out.end - out.start);
}

// What a face of a pass at a column depends on besides its layer.
struct PassSite {
    const StrataPass* pass = nullptr;
    double strength = 0.0; // the mask of the walls the pass stands in
    double share = 1.0;    // of the nearest cell of the joints in the top (1/2 on a fracture)
    double lift = 0.0;
    double ground = 0.0; // under the pass at the column
    double rate = 1.0;   // of that ground with the ground of the walls
    double liftX = 0.0;
    double liftZ = 0.0;
    double noise = 0.0; // of the overhangs along the walls
    double shift = 0.0; // of the bands of the grain
};

// The height of the top of a pass where its ground is lifted to g, over the top at the column.
double relative(const PassSite& s, double g) {
    return passTop(*s.pass, s.lift, g - s.lift) - passTop(*s.pass, s.lift, s.ground);
}

// Adds the face of a riser of a pass the column lies within.
void addRiserFace(const PassSite& s, const Riser& r, Site& site) {
    const StrataPass& p = *s.pass;
    const std::int64_t k = r.k;
    const double foot = relative(s, r.start);
    const double top = relative(s, r.end);
    const double height = top - foot;
    // Across a fracture of the crystalline rock the joints of the next cell take over: the face
    // fades out as its cell gives way (kWholeCell of the top).
    const double weight = smooth(kFaintLow, kFaintHigh, s.strength * r.layer.hardness) *
                          smooth(0.5 * kLeastRockFace, kLeastRockFace, height) *
                          smooth(0.5, kWholeCell, s.share);
    if (!(weight > 0.0) || site.count >= site.pending.size()) {
        return;
    }
    PendingFace& out = site.pending[site.count++];
    out = {};
    RockFace& face = out.face;
    face.foot = static_cast<float>(foot);
    face.top = static_cast<float>(top);
    const double capDraw = (1.0 - kCapNoise) * mountain_walls::layerDraw(p, k, 0) +
                           kCapNoise * (0.5 + 0.5 * s.noise);
    const double cutDraw = (1.0 - kCapNoise) * mountain_walls::layerDraw(p, k, 1) +
                           kCapNoise * (0.5 - 0.5 * s.noise);
    face.cap = static_cast<float>(std::min(kCapShare * height, kCapMost) * weight *
                                  smooth(kCapFrom, kCapFull, capDraw));
    face.capDepth = static_cast<float>(
        height * (kCapDepthLow + kCapDepthSpread * mountain_walls::layerDraw(p, k, 2)));
    face.undercut = static_cast<float>(std::min(kCutShare * height, kCutMost) * weight *
                                       smooth(kCutFrom, kCutFull, cutDraw));
    face.undercutHeight = static_cast<float>(
        height * (kCutHeightLow + kCutHeightSpread * mountain_walls::layerDraw(p, k, 3)));
    face.grain = static_cast<float>(std::min(kGrainShare * height, kGrainMost) * weight);
    face.grainShift = static_cast<float>(s.shift);
    face.weight = static_cast<float>(weight);
    out.level = r.level;
    out.rise = r.end - r.start;
    out.rate = s.rate;
    out.liftX = s.liftX;
    out.liftZ = s.liftZ;
}

} // namespace

Grain makeGrain(std::uint64_t seed, const WorldSize& world) {
    const auto octaves = [&](std::uint64_t stream, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = 2,
                                  .frequency = 1.0 / wavelength,
                                  .lattices = noise::OctaveLattices::RotatedFast},
                                 world);
    };
    return {octaves(2, kOverhangWavelength), octaves(0, kShiftWavelength)};
}

void addFaces(const mountain_walls::Noises& noises, const Grain& grain,
              const mountain_walls::Masks& masks, const mountain_walls::Column& column, double x,
              double z, mountain_walls::Memo* memo, Site& site) {
    // The weights of the passes are their masks at most, and the hardness of a layer is 1 at most.
    if (!(std::max(masks.strata, masks.crystal) > kFaintLow)) {
        return;
    }
    const mountain_walls::WallPasses w =
        mountain_walls::wallPasses(noises, masks, x, z, column.gradX, column.gradZ, memo);
    bool noisy = false;
    double noise = 0.0;
    double shift = 0.0;
    for (const std::size_t i : kFacePasses) {
        const StrataPass& p = w.passes[i];
        const double strength = i == 1 ? masks.strata : p.weight;
        Riser riser;
        if (!(p.weight > 0.0) || !(strength > kFaintLow)) {
            continue;
        }
        const double ground = groundUnder(w, column.ground, i);
        if (!riserAt(p, ground + w.lift[i], riser)) {
            continue;
        }
        // How fast the ground under the pass rises with the ground of the walls (1 for the first).
        const double rate = i == 0 ? 1.0
                                   : (groundUnder(w, column.ground + kDelta, i) -
                                      groundUnder(w, column.ground - kDelta, i)) /
                                         (2.0 * kDelta);
        if (!noisy) {
            noise = static_cast<double>(grain.overhangs.fbm2D(x, z));
            shift = kShiftRange * static_cast<double>(grain.shift.fbm2D(x, z));
            noisy = true;
        }
        const PassSite pass{&p,         strength,   w.share[i], w.lift[i], ground,
                            std::max(0.0, rate), w.liftX[i], w.liftZ[i], noise, shift};
        addRiserFace(pass, riser, site);
    }
}

RockFace placeFace(const PendingFace& pending, double gradX, double gradZ) {
    const double riseX = pending.rate * gradX + pending.liftX;
    const double riseZ = pending.rate * gradZ + pending.liftZ;
    const double slope = std::max(dmath::hypot(riseX, riseZ), kLeastSlope);
    RockFace face = pending.face;
    face.distance = static_cast<float>(pending.level / slope);
    face.run = static_cast<float>(pending.rise / slope);
    // Downhill; straight along x where the ground is flat (the run is then long and the face
    // faint anyway).
    const double length = dmath::hypot(riseX, riseZ);
    face.airX = static_cast<float>(length > 0.0 ? -riseX / length : 1.0);
    face.airZ = static_cast<float>(length > 0.0 ? -riseZ / length : 0.0);
    // The grain stands out twice its amplitude at most, the overhangs within the rest of the run.
    face.grain =
        static_cast<float>(std::min(static_cast<double>(face.grain), kGrainRun * face.run));
    const double room = std::max(0.0, kWithinRun * face.run - 2.0 * face.grain);
    face.cap = static_cast<float>(std::min(static_cast<double>(face.cap), room));
    face.undercut = static_cast<float>(std::min(static_cast<double>(face.undercut), room));
    return face;
}

} // namespace terrain::wall_faces
