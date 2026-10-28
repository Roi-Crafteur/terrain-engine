#include "terrain/generation/rock_faces.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain {

namespace {

// The features a LOD 3 tile draws: half of its cells of 8 m.
constexpr float kCoarseFeature = 4.0f;
// rockShare: the rock reaches this far below the foot of a face (its floor under a cap, the sill
// of a niche) and above its top, and the face this far in front of and behind its middle.
constexpr double kShareBelow = 2.0;
constexpr double kShareAbove = 0.5;
constexpr double kShareAcross = 3.0;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

bool kept(float feature, float extent, const RockDetail& detail) {
    return feature > 0.0f && extent > 0.0f && feature >= detail.leastFeature;
}

// How far the face lies toward the air at height y of its step: its cap, full over the top two
// fifths of its depth and nothing at its bottom (a ceiling rising to the lip), less its undercut,
// an arch deepest at the foot.
double offsetAt(const RockFace& f, double y, const RockDetail& detail) {
    double offset = 0.0;
    if (kept(f.cap, f.capDepth, detail)) {
        offset += f.cap * smooth(static_cast<double>(f.top) - f.capDepth,
                                 static_cast<double>(f.top) - 0.6 * f.capDepth, y);
    }
    if (kept(f.undercut, f.undercutHeight, detail)) {
        const double t = (y - static_cast<double>(f.foot)) / f.undercutHeight;
        if (t >= 0.0 && t < 1.0) {
            offset -= f.undercut * std::sqrt(1.0 - t * t);
        }
    }
    return offset;
}

// The height of the relief above the line of the run from the foot to the top at the column.
double residual(const RockFace& f, double relief) {
    const double line = 0.5 * (static_cast<double>(f.foot) + f.top) -
                        (static_cast<double>(f.top) - f.foot) * f.distance / f.run;
    return relief - line;
}

// The density of the step of a face at height y of a column whose relief is at relief: the rock
// below its top behind the face and below its foot in front of it, both lifted by the residual.
double stepDensity(const RockFace& f, double y, double relief, const RockDetail& detail) {
    const double v = y - residual(f, relief);
    const double across = static_cast<double>(f.distance) - offsetAt(f, v, detail);
    return std::min(v - f.foot, std::max(v - f.top, across));
}

// The relief at height y (density y - relief) and the steps of the faces of one kind (carved or
// not) blended by their weights.
double blendedDensity(const RockColumn& c, double y, bool carved, double relief,
                      const RockDetail& detail) {
    double weights = 0.0;
    double steps = 0.0;
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        const RockFace& f = c.faces[i];
        if (f.carved != carved || !(f.weight > 0.0f) || !(f.run > 0.0f)) {
            continue;
        }
        weights += f.weight;
        steps += f.weight * stepDensity(f, y, relief, detail);
    }
    if (weights > 1.0) {
        return steps / weights;
    }
    return (1.0 - weights) * (y - relief) + steps;
}

} // namespace

void RockColumn::add(const RockFace& face) {
    if (faceCount < kMaxRockFaces) {
        faces[faceCount++] = face;
        return;
    }
    const auto weakest = std::min_element(
        faces.begin(), faces.end(),
        [](const RockFace& a, const RockFace& b) { return a.weight < b.weight; });
    if (face.weight > weakest->weight) {
        *weakest = face;
    }
}

RockDetail rockDetailForLod(int lod) {
    return {lod >= 3 ? kCoarseFeature : 0.0f};
}

float rockDensity(const RockColumn& c, double y, const RockDetail& detail) {
    if (c.faceCount == 0) {
        return static_cast<float>(y - c.ground);
    }
    // The relief before the rivers, then the air of their valleys taken out of it (below the
    // relief, above their floor), then their levees added.
    double density = blendedDensity(c, y, false, c.uncarved, detail);
    const bool carvedFaces = std::any_of(c.faces.begin(), c.faces.begin() + c.faceCount,
                                         [](const RockFace& f) { return f.carved; });
    if (c.ground < c.uncarved || carvedFaces) {
        const double valley = blendedDensity(c, y, true, c.ground, detail);
        density = std::max(density, std::min(valley, static_cast<double>(c.uncarved) - y));
    }
    if (c.ground > c.uncarved) {
        density = std::min(density, y - static_cast<double>(c.ground));
    }
    return static_cast<float>(density);
}

void anchorFace(RockFace& face, float foot, float top) {
    const double before = static_cast<double>(face.top) - face.foot;
    const double after = static_cast<double>(top) - foot;
    const double scale = before > 0.0 ? std::max(0.0, after / before) : 0.0;
    face.foot = foot;
    face.top = top;
    face.capDepth = static_cast<float>(face.capDepth * scale);
    face.undercutHeight = static_cast<float>(face.undercutHeight * scale);
    face.cap = static_cast<float>(face.cap * std::min(1.0, scale));
    face.undercut = static_cast<float>(face.undercut * std::min(1.0, scale));
    face.weight = static_cast<float>(face.weight * smooth(kLeastRockFace, 2.0 * kLeastRockFace,
                                                          after));
}

RockBounds rockBounds(const RockColumn& c) {
    RockBounds b{std::min(c.ground, c.uncarved), std::max(c.ground, c.uncarved)};
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        const RockFace& f = c.faces[i];
        const double lift = residual(f, f.carved ? c.ground : c.uncarved);
        b.low = std::min(b.low, static_cast<float>(f.foot + lift));
        b.high = std::max(b.high, static_cast<float>(f.top + lift));
    }
    return b;
}

float rockShare(const RockColumn& c, double y) {
    float share = 0.0f;
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        const RockFace& f = c.faces[i];
        const double v = y - residual(f, f.carved ? c.ground : c.uncarved);
        const double distance = f.distance;
        if (v >= f.foot - kShareBelow && v <= f.top + kShareAbove &&
            distance < std::max(0.0f, f.cap) + kShareAcross &&
            distance > -(std::max(0.0f, f.undercut) + kShareAcross)) {
            share = std::max(share, std::min(1.0f, f.weight));
        }
    }
    return share;
}

} // namespace terrain
