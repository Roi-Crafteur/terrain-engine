#include "terrain/generation/rock_faces.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain {

namespace {

// The features a LOD 3 tile draws: half of its cells of 8 m.
constexpr float kCoarseFeature = 4.0f;
// rockShare: the rock reaches this far below the foot of a face (its floor under a cap, the sill
// of a niche) and above its top, and the face this far in front of and behind the column.
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

// How far the face lies toward the air at height y: its cap, full over the top two fifths of its
// depth and nothing at its bottom (a ceiling rising to the lip), less its undercut, an arch deepest
// at the foot.
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

// The density of the faces of one kind (carved or not) whose heights hold y, the nearest of them;
// otherwise beyond, the density given.
double facesDensity(const RockColumn& c, double y, bool carved, double beyond,
                    const RockDetail& detail) {
    double best = std::numeric_limits<double>::infinity();
    double density = beyond;
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        const RockFace& f = c.faces[i];
        if (f.carved != carved || !(y >= f.foot) || !(y <= f.top)) {
            continue;
        }
        const double across = static_cast<double>(f.distance) - offsetAt(f, y, detail);
        if (std::abs(across) < best) {
            best = std::abs(across);
            density = across;
        }
    }
    return density;
}

} // namespace

void RockColumn::add(const RockFace& face) {
    if (faceCount < kMaxRockFaces) {
        faces[faceCount++] = face;
        return;
    }
    const auto farthest = std::max_element(
        faces.begin(), faces.end(), [](const RockFace& a, const RockFace& b) {
            return std::abs(a.distance) < std::abs(b.distance);
        });
    if (std::abs(face.distance) < std::abs(farthest->distance)) {
        *farthest = face;
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
    // relief, above their surface or in front of their faces), then their levees added.
    double density = facesDensity(c, y, false, y - static_cast<double>(c.uncarved), detail);
    const bool carvedFaces = std::any_of(c.faces.begin(), c.faces.begin() + c.faceCount,
                                         [](const RockFace& f) { return f.carved; });
    if (c.ground < c.uncarved || carvedFaces) {
        const double valley = facesDensity(c, y, true, y - static_cast<double>(c.ground), detail);
        density = std::max(density, std::min(valley, static_cast<double>(c.uncarved) - y));
    }
    if (c.ground > c.uncarved) {
        density = std::min(density, y - static_cast<double>(c.ground));
    }
    return static_cast<float>(density);
}

RockBounds rockBounds(const RockColumn& c) {
    RockBounds b{std::min(c.ground, c.uncarved), std::max(c.ground, c.uncarved)};
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        b.low = std::min(b.low, c.faces[i].foot);
        b.high = std::max(b.high, c.faces[i].top);
    }
    return b;
}

float rockShare(const RockColumn& c, double y) {
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        const RockFace& f = c.faces[i];
        const double distance = f.distance;
        if (y >= f.foot - kShareBelow && y <= f.top + kShareAbove &&
            distance < std::max(0.0f, f.cap) + kShareAcross &&
            distance > -(std::max(0.0f, f.undercut) + kShareAcross)) {
            return 1.0f;
        }
    }
    return 0.0f;
}

} // namespace terrain
