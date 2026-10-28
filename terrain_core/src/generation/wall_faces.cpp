// The faces of the walls of the mountains in three dimensions (wall_faces.hpp).
#include "wall_faces.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace terrain::wall_faces {

namespace {

using mountain_walls::StrataPass;

// The faces of the walls fade in where their mask times the hardness of their layer runs from
// kFaintLow (the slope of the relief) to kFaintHigh (vertical).
constexpr double kFaintLow = 0.3;
constexpr double kFaintHigh = 0.6;
// Faces lower than kLeastFace meters keep the slope of the relief.
constexpr double kLeastFace = 3.0;
// The cap of a face juts out up to kCapShare of its height, kCapMost meters at most, over the top
// kCapDepthLow to kCapDepthLow + kCapDepthSpread of it; its undercut recedes up to kCutShare of its
// height, kCutMost meters at most, up to kCutHeightLow to kCutHeightLow + kCutHeightSpread of it.
constexpr double kCapShare = 0.12;
constexpr double kCapMost = 8.0;
constexpr double kCapDepthLow = 0.12;
constexpr double kCapDepthSpread = 0.18;
constexpr double kCutShare = 0.1;
constexpr double kCutMost = 6.0;
constexpr double kCutHeightLow = 0.1;
constexpr double kCutHeightSpread = 0.2;
// A face has a cap where the draw of its layer and the noise along it (kCapNoise of it) pass
// kCapFrom to kCapFull, an undercut where they pass kCutFrom to kCutFull (the noise reversed: the
// rock under a cap is often whole).
constexpr double kCapNoise = 0.45;
constexpr double kCapFrom = 0.45;
constexpr double kCapFull = 0.8;
constexpr double kCutFrom = 0.5;
constexpr double kCutFull = 0.85;
// The columns within half the run of a face, its cap or its undercut and kReach more meters see it.
constexpr double kReach = 3.0;
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

// The grounds under the passes of a column over ground: the ground the first one lays its layers
// on, then the top each one leaves to the next.
std::array<double, 4> passGrounds(const mountain_walls::WallPasses& w, double ground) {
    std::array<double, 4> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = ground;
        ground = passTop(w.passes[i], w.lift[i], ground);
    }
    return out;
}

// What a face of a pass at a column depends on besides its layer.
struct PassSite {
    const StrataPass* pass = nullptr;
    double lift = 0.0;
    double ground = 0.0; // under the pass at the column
    double slope = 0.0;  // of that ground along the gradient, meters per meter
    double noise = 0.0;  // of the overhangs along the walls
};

// The height of the top of a pass where its ground is lifted to g, over the top at the column.
double relative(const PassSite& s, double g) {
    return passTop(*s.pass, s.lift, g - s.lift) - passTop(*s.pass, s.lift, s.ground);
}

// Adds the face of layer k of a pass, if the column sees it.
void addLayerFace(const PassSite& s, std::int64_t k, RockColumn& rock) {
    const StrataPass& p = *s.pass;
    const mountain_walls::LayerFace layer = mountain_walls::layerFace(p, k);
    const double vertical = smooth(kFaintLow, kFaintHigh, p.weight * layer.hardness);
    if (!(vertical > 0.0) || !(layer.end > layer.start)) {
        return;
    }
    const double recede = p.rim * std::max(p.slope, kLeastSlope) + p.offset;
    const double start = layer.start * p.band + recede;
    const double end = layer.end * p.band + recede;
    const double foot = relative(s, start);
    const double top = relative(s, end);
    const double height = top - foot;
    if (!(height >= kLeastFace)) {
        return;
    }
    const double run = (end - start) / s.slope;
    const double distance = (0.5 * (start + end) - (s.ground + s.lift)) / s.slope;
    const double cap = std::min(kCapShare * height, kCapMost) * vertical *
                       smooth(kCapFrom, kCapFull,
                              (1.0 - kCapNoise) * mountain_walls::layerDraw(p, k, 0) +
                                  kCapNoise * (0.5 + 0.5 * s.noise));
    const double undercut = std::min(kCutShare * height, kCutMost) * vertical *
                            smooth(kCutFrom, kCutFull,
                                   (1.0 - kCapNoise) * mountain_walls::layerDraw(p, k, 1) +
                                       kCapNoise * (0.5 - 0.5 * s.noise));
    if (std::abs(distance) > 0.5 * run + cap + undercut + kReach) {
        return;
    }
    RockFace face;
    face.foot = static_cast<float>(foot);
    face.top = static_cast<float>(top);
    face.distance = static_cast<float>(distance);
    face.cap = static_cast<float>(cap);
    face.capDepth = static_cast<float>(
        height * (kCapDepthLow + kCapDepthSpread * mountain_walls::layerDraw(p, k, 2)));
    face.undercut = static_cast<float>(undercut);
    face.undercutHeight = static_cast<float>(
        height * (kCutHeightLow + kCutHeightSpread * mountain_walls::layerDraw(p, k, 3)));
    face.run = static_cast<float>(run);
    face.vertical = static_cast<float>(vertical);
    rock.add(face);
}

} // namespace

void addFaces(const mountain_walls::Noises& noises, const mountain_walls::Masks& masks,
              const mountain_walls::Column& column, double x, double z,
              mountain_walls::Memo* memo, RockColumn& rock) {
    const mountain_walls::WallPasses w =
        mountain_walls::wallPasses(noises, masks, x, z, column.gradX, column.gradZ, memo);
    const std::array<double, 4> grounds = passGrounds(w, column.ground);
    // How fast the ground under each pass rises with the ground of the walls.
    const std::array<double, 4> above = passGrounds(w, column.ground + kDelta);
    const std::array<double, 4> below = passGrounds(w, column.ground - kDelta);
    const double noise = mountain_walls::overhangNoise(noises, x, z, memo);
    for (std::size_t i = 0; i < w.passes.size(); ++i) {
        const StrataPass& p = w.passes[i];
        if (!(p.weight > 0.0)) {
            continue;
        }
        const double rate = (above[i] - below[i]) / (2.0 * kDelta);
        const PassSite site{&p, w.lift[i], grounds[i],
                            std::max(p.slope * std::max(rate, 0.0), kLeastSlope), noise};
        const std::int64_t k = mountain_walls::layerOf(p, grounds[i] + w.lift[i]);
        addLayerFace(site, k - 1, rock);
        addLayerFace(site, k, rock);
    }
}

} // namespace terrain::wall_faces
