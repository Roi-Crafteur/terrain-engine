#include "terrain/generation/rock_faces.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace terrain {

namespace {

// The grain octaves a detail draws: the fine one on cells of kFineCell meters at most, the coarse
// one on cells of kCoarseCell meters at most.
constexpr float kFineCell = 1.0f;
constexpr float kCoarseCell = 4.0f;
// rockShare: the rock reaches this far below the foot of a face (its floor under a cap, the sill
// of a niche) and above its top, and the face this far in front of and behind its middle.
constexpr double kShareBelow = 2.0;
constexpr double kShareAbove = 0.5;
constexpr double kShareAcross = 3.0;

// The grain of the faces: a value noise along the height, its values kGrainWave meters apart, and
// a finer one (kGrainFine of it, kGrainFineWave meters) the coarse detail leaves out; the flutes
// add kGrainFlutes of grainAlong.
constexpr double kGrainWave = 7.9;
constexpr double kGrainFineWave = 3.1;
constexpr double kGrainFine = 0.4;
constexpr double kGrainFlutes = 0.6;
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

// The value of octave salt at lattice point i, in [-1, 1).
double valueAt(std::int64_t i, std::uint64_t salt) {
    const std::uint64_t hash = noise::deriveSeed(salt, static_cast<std::uint64_t>(i));
    return static_cast<double>(hash >> 11) * 0x1.0p-52 - 1.0;
}

// Value noise along t: the values of the lattice points smoothly joined (no zero at them).
double valueNoise(double t, std::uint64_t salt) {
    const double cell = std::floor(t);
    const auto i = static_cast<std::int64_t>(cell);
    const double f = t - cell;
    const double s = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    const double a = valueAt(i, salt);
    return a + s * (valueAt(i + 1, salt) - a);
}

double grainAt(const RockFace& f, double y, const RockDetail& detail) {
    if (!(f.grain > 0.0f)) {
        return 0.0;
    }
    if (!(detail.cell <= kCoarseCell)) {
        return 0.0;
    }
    const double t = y + static_cast<double>(f.grainShift);
    double g = valueNoise(t / kGrainWave, kGrainSalt) + kGrainFlutes * f.grainAlong;
    if (detail.cell <= kFineCell) {
        g += kGrainFine * valueNoise(t / kGrainFineWave, kGrainSalt + 1);
    }
    return f.grain * g;
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

// The height of the relief above the line of the run from the foot to the top at the column.
double residual(const RockFace& f, double relief) {
    const double line = 0.5 * (static_cast<double>(f.foot) + f.top) -
                        (static_cast<double>(f.top) - f.foot) * f.distance / f.run;
    return relief - line;
}

// The density of the step of a face at height y of a column whose relief is at relief: the rock
// below its top behind the face and below its foot in front of it, both lifted by the residual,
// its edges rounded.
double stepDensity(const RockFace& f, double y, double relief, const RockDetail& detail) {
    const double v = y - residual(f, relief);
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
        steps += weight * stepDensity(f, y, relief, detail);
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
                                   [&](const RockFace& f) { return drawnWeight(f, detail) > 0.0f; });
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
        const RockFace& f = c.faces[i];
        const double lift = residual(f, f.carved ? c.ground : c.uncarved);
        b.low = std::min(b.low, static_cast<float>(f.foot + lift));
        b.high = std::max(b.high, static_cast<float>(f.top + lift));
    }
    return b;
}

float rockShare(const RockColumn& c, double y, const RockDetail& detail) {
    float share = 0.0f;
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        const RockFace& f = c.faces[i];
        const double v = y - residual(f, f.carved ? c.ground : c.uncarved);
        const double distance = f.distance;
        if (v >= f.foot - kShareBelow && v <= f.top + kShareAbove &&
            distance < std::max(0.0f, f.cap) + kShareAcross &&
            distance > -(std::max(0.0f, f.undercut) + kShareAcross)) {
            share = std::max(share, std::min(1.0f, drawnWeight(f, detail)));
        }
    }
    return share;
}

} // namespace terrain
