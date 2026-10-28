#include "terrain/generation/rock_faces.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace terrain {

namespace {

// rockShare: the rock reaches this far below the foot of a face (its floor under a cap, the sill
// of a niche) and above its top, and the face this far in front of and behind its middle.
constexpr double kShareBelow = 2.0;
constexpr double kShareAbove = 0.5;
constexpr double kShareAcross = 3.0;

// The grain of the faces: beds of rock along the height, kBedWave meters thick on average (from
// 1 - kBedJitter to 1 + kBedJitter of it), each standing out or receding by up to the grain, the
// one to the next over kBedEdge meters (a noise along the height would set its features about one
// lattice apart: a regular row of bands). Cells larger than kGrainCell leave it out.
constexpr double kBedWave = 6.5;
constexpr double kBedJitter = 0.42;
constexpr double kBedEdge = 2.0;
constexpr float kGrainCell = 2.0f;
constexpr std::uint64_t kGrainSalt = 0x677261696e5f3132ULL;
constexpr double kRunFade = 1.5;
constexpr double kFeatureLow = 1.5;
constexpr double kFeatureHigh = 3.0;
// The edges of a step are rounded over kRound meters, kRoundShare of its height at most.
constexpr double kRound = 3.0;
constexpr double kRoundShare = 0.25;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A share in [0, 1) drawn for bed k, for stream.
double bedDraw(std::int64_t k, std::uint64_t stream) {
    const std::uint64_t hash =
        noise::deriveSeed(kGrainSalt + stream, static_cast<std::uint64_t>(k));
    return static_cast<double>(hash >> 11) * 0x1.0p-53;
}

// The height along t where bed k starts.
double bedStart(std::int64_t k) {
    return (static_cast<double>(k) + kBedJitter * (bedDraw(k, 0) - 0.5)) * kBedWave;
}

// How far bed k stands out, in [-1, 1).
double bedOut(std::int64_t k) {
    return 2.0 * bedDraw(k, 1) - 1.0;
}

double grainAt(const RockFace& f, double y, const RockDetail& detail) {
    if (!(f.grain > 0.0f) || !(detail.cell <= kGrainCell)) {
        return 0.0;
    }
    const double t = y + static_cast<double>(f.grainShift);
    auto k = static_cast<std::int64_t>(std::floor(t / kBedWave));
    if (t < bedStart(k)) {
        --k;
    } else if (t >= bedStart(k + 1)) {
        ++k;
    }
    // The bed, joined to the one below and to the one above at its edges.
    const double low = bedStart(k);
    const double high = bedStart(k + 1);
    double out = bedOut(k);
    out += (bedOut(k - 1) - out) * (1.0 - smooth(low - 0.5 * kBedEdge, low + 0.5 * kBedEdge, t));
    out += (bedOut(k + 1) - bedOut(k)) *
           smooth(high - 0.5 * kBedEdge, high + 0.5 * kBedEdge, t);
    return f.grain * out;
}

// Polynomial smooth maximum and minimum over r (the plain ones beyond it).
double smoothMax(double a, double b, double r) {
    const double h = std::max(r - std::abs(a - b), 0.0) / r;
    return std::max(a, b) + 0.25 * h * h * r;
}

double smoothMin(double a, double b, double r) {
    return -smoothMax(-a, -b, r);
}

// How much of an overhang of feature meters a detail draws: none under kFeatureLow cells, all of
// it from kFeatureHigh cells (narrower, the cells would catch it at one column out of two).
double drawnFeature(float feature, float extent, const RockDetail& detail) {
    if (!(feature > 0.0f) || !(extent > 0.0f)) {
        return 0.0;
    }
    return feature * smooth(kFeatureLow * detail.cell, kFeatureHigh * detail.cell, feature);
}

// How far the face lies toward the air at height y of its step: its grain, its cap, full over the
// top two fifths of its depth and nothing at its bottom (a ceiling rising to the lip), less its
// undercut, an arch deepest at the foot.
double offsetAt(const RockFace& f, double y, const RockDetail& detail) {
    double offset = grainAt(f, y, detail);
    const double cap = drawnFeature(f.cap, f.capDepth, detail);
    if (cap > 0.0) {
        offset += cap * smooth(static_cast<double>(f.top) - f.capDepth,
                               static_cast<double>(f.top) - 0.6 * f.capDepth, y);
    }
    const double undercut = drawnFeature(f.undercut, f.undercutHeight, detail);
    if (undercut > 0.0) {
        const double t = (y - static_cast<double>(f.foot)) / f.undercutHeight;
        if (t >= 0.0 && t < 1.0) {
            offset -= undercut * std::sqrt(1.0 - t * t);
        }
    }
    return offset;
}

// The density of the step of a face at height y: the rock below its top behind the face and below
// its foot in front of it, its edges rounded.
double stepDensity(const RockFace& f, double v, const RockDetail& detail) {
    const double across = static_cast<double>(f.distance) - offsetAt(f, v, detail);
    const double round = std::min(kRound, kRoundShare * (static_cast<double>(f.top) - f.foot));
    if (!(round > 0.0)) {
        return std::min(v - f.foot, std::max(v - f.top, across));
    }
    return smoothMin(v - f.foot, smoothMax(v - f.top, across, round), round);
}

// The relief at height y (density y - relief) and the steps of the faces of one kind (carved or
// not) blended by their weights.
double blendedDensity(const RockColumn& c, double y, bool carved, double relief,
                      const RockDetail& detail) {
    double weights = 0.0;
    double steps = 0.0;
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        const RockFace& f = c.faces[i];
        const double weight = drawnWeight(f, detail);
        if (f.carved != carved || !(weight > 0.0)) {
            continue;
        }
        weights += weight;
        steps += weight * stepDensity(f, y, detail);
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
    return {static_cast<float>(1 << std::clamp(lod, 0, 16))};
}

float rockDensity(const RockColumn& c, double y, const RockDetail& detail) {
    const bool drawn = std::any_of(c.faces.begin(), c.faces.begin() + c.faceCount,
                                   [&](const RockFace& f) {
                                       return drawnWeight(f, detail) > 0.0f;
                                   });
    if (!drawn) {
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

float drawnWeight(const RockFace& f, const RockDetail& detail) {
    const double least = static_cast<double>(kRunCells) * detail.cell;
    return static_cast<float>(f.weight * smooth(least, kRunFade * least, f.run));
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
        b.low = std::min(b.low, c.faces[i].foot);
        b.high = std::max(b.high, c.faces[i].top);
    }
    return b;
}

float rockShare(const RockColumn& c, double y, const RockDetail& detail) {
    float share = 0.0f;
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        const RockFace& f = c.faces[i];
        const double distance = f.distance;
        if (y >= f.foot - kShareBelow && y <= f.top + kShareAbove &&
            distance < std::max(0.0f, f.cap) + kShareAcross &&
            distance > -(std::max(0.0f, f.undercut) + kShareAcross)) {
            share = std::max(share, std::min(1.0f, drawnWeight(f, detail)));
        }
    }
    return share;
}

} // namespace terrain
