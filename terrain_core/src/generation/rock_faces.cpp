#include "terrain/generation/rock_faces.hpp"

#include "rock_spires.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

namespace terrain {

namespace {

// rockShare: the rock reaches this far below the foot of a face (its floor under a cap, the sill
// of a niche) and above its top, and the face this far in front of and behind its middle.
constexpr double kShareBelow = 2.0;
constexpr double kShareAbove = 0.5;
constexpr double kShareAcross = 3.0;

// The grain of the faces: beds of rock along the height, kBedWave meters thick on average (from
// 1 - kBedJitter to 1 + kBedJitter of it), each standing out or receding by up to the grain, the
// one to the next over kBedEdge meters, kBedEdgeCells cells at least (a noise along the height
// would set its features about one lattice apart: a regular row of bands; an edge sharper than
// the cells, wandering along the face, would step at each of them). Cells larger than kGrainCell
// leave it out.
constexpr double kBedWave = 6.5;
constexpr double kBedJitter = 0.42;
constexpr double kBedEdge = 2.0;
constexpr double kBedEdgeCells = 1.5;
constexpr float kGrainCell = 2.0f;
// The beds of a face show fully in its bands from a grain of kLayeredGrain meters (rockBands), their
// coordinates counted from a base every kStrataHeight meters of height.
constexpr float kLayeredGrain = 0.5f;
constexpr double kStrataHeight = 256.0;
constexpr std::uint64_t kGrainSalt = 0x677261696e5f3132ULL;
constexpr double kRunFade = 1.5;
constexpr double kFeatureLow = 1.5;
constexpr double kFeatureHigh = 3.0;
constexpr double kThickness = 0.5;
constexpr double kJointCells = 4.0;
// The edges of a step are rounded over kRound meters, kRoundShare of its height at most; those of
// the faces the rivers carve (their slots, fresh rock) over kCarvedRound meters at most, so that
// the rounding of their foot leaves their notch (slot_faces.hpp) alone.
constexpr double kRound = 3.0;
constexpr double kCarvedRound = 1.0;
constexpr double kRoundShare = 0.25;
// The tops of the faces of their own rounding (RockFace::roundTenths: the coasts, sharp at a
// meter), where the cells are coarser than a meter: an edge sharper than the cells steps at each
// one along a face oblique to them (a saw along its top: the check in Unreal of 06fd02f), rounded
// over a few cells its facets turn too much, each turning from a sun behind the wall in the shadow
// of the others while its vertices light it (a row of teeth of shadow under its top: d8e97ed). A
// bevel at 45 degrees kRoundCells cells high, planes the cells draw exactly, its creases with the
// top and the face rounded over kUpperCrease and kLowerCrease of it. On the calanque of the view 5
// (coast_top_test): the part of its sky line at the lattice of the cells 0.063 m with an edge of a
// meter, 0.025 m rounded, 0.033 m bevelled; the shadows under suns behind it up to 18 % of the
// light of its top rounded (0.008 at the lattice), 7 % bevelled (0.0026).
constexpr double kRoundCells = 1.0;
constexpr double kUpperCrease = 0.7;
constexpr double kLowerCrease = 0.5;
// The grain and the overhangs of a face come in as its drawn weight runs from kFullLow to 1.
constexpr double kFullLow = 0.75;
// The joints come in over kJointEnds meters over the foot of a face (their floor rising out of
// it) and go out under its top over 1.5 / kJointRoof times their depth at least: the rock closing
// over a groove is a roof, and steeper it would turn its face down, black under the light. Both
// ends take kJointEndShare of the height of the face at most, the depth of the joint within what
// fits there: a groove never ends cut short at the foot or the top of its face.
constexpr double kJointEnds = 4.0;
constexpr double kJointRoof = 0.5;
constexpr double kJointEndShare = 0.25;
// A joint opens over stretches of its height, never all of it: along the height, cells of
// kStretchCell to kStretchCell + kStretchCellSpread meters (by the draw of the joint, shifted by
// it) each hold a stretch kStretchOpen of the time, kStretchLow to kStretchHigh meters long, at a
// drawn place within it half kStretchGap from its ends at least (two stretches kStretchGap apart
// at least). Each end of a stretch fades as the ends of the face do, over half the stretch at most
// (its depth within what fits).
constexpr double kStretchCell = 22.0;
constexpr double kStretchCellSpread = 14.0;
constexpr double kStretchOpen = 0.7;
constexpr double kStretchLow = 8.0;
constexpr double kStretchHigh = 30.0;
constexpr double kStretchGap = 6.0;
constexpr std::uint64_t kStretchSalt = 0x73747265746368ULL;

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
    const double edge = std::max(kBedEdge, kBedEdgeCells * static_cast<double>(detail.cell));
    double out = bedOut(k);
    out += (bedOut(k - 1) - out) * (1.0 - smooth(low - 0.5 * edge, low + 0.5 * edge, t));
    out += (bedOut(k + 1) - bedOut(k)) * smooth(high - 0.5 * edge, high + 0.5 * edge, t);
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

// How much of an overhang of feature meters over extent meters of the height of its face a detail
// draws: none under kFeatureLow cells, all of it from kFeatureHigh cells, of its reach and of its
// thickness, kThickness of its extent at its tip (narrower or thinner, the cells would catch it at
// one column or one height out of two).
double drawnFeature(float feature, float extent, const RockDetail& detail) {
    if (!(feature > 0.0f) || !(extent > 0.0f)) {
        return 0.0;
    }
    const double low = kFeatureLow * detail.cell;
    const double high = kFeatureHigh * detail.cell;
    return feature * smooth(low, high, feature) * smooth(low, high, kThickness * extent);
}

// The edges of the step of a face are rounded over this many meters as cells of the detail draw
// them (its top bevelled over that much: kRoundCells).
double roundOf(const RockFace& f, const RockDetail& detail) {
    const auto cell = static_cast<double>(detail.cell);
    const double own = std::max(0.1 * static_cast<double>(f.roundTenths),
                                cell > 1.0 ? kRoundCells * cell : 0.0);
    const double round = f.roundTenths > 0 ? own : (f.carved ? kCarvedRound : kRound);
    return std::min(round, kRoundShare * (static_cast<double>(f.top) - f.foot));
}

// Draw k (0, 1 or 2) in [0, 1) of 21 bits of a hash.
double drawOf(std::uint64_t hash, unsigned k) {
    return static_cast<double>((hash >> (21U * k)) & 0x1F'FFFFU) * 0x1.0p-21;
}

// The stretch of the joint of a face whose cell holds height y: its lowest and highest heights
// (low >= high: none there).
std::array<double, 2> stretchAt(const RockFace& f, double y) {
    const std::uint64_t joint = noise::deriveSeed(
        kStretchSalt, static_cast<std::uint64_t>(static_cast<double>(f.jointSeed) * 0x1.0p24));
    const double cell = kStretchCell + kStretchCellSpread * drawOf(joint, 0);
    const double shift = cell * drawOf(joint, 1);
    const auto n = static_cast<std::int64_t>(std::floor((y + shift) / cell));
    const std::uint64_t hash = noise::deriveSeed(joint, static_cast<std::uint64_t>(n));
    if (!(drawOf(hash, 0) < kStretchOpen)) {
        return {0.0, 0.0};
    }
    const double longest = std::min(kStretchHigh, cell - kStretchGap);
    const double length = kStretchLow + (longest - kStretchLow) * drawOf(hash, 1);
    const double low = static_cast<double>(n) * cell - shift + 0.5 * kStretchGap +
                       (cell - kStretchGap - length) * drawOf(hash, 2);
    return {low, low + length};
}

// How far the joint of a face recedes it at height y: within a stretch, both of its ends and
// those of the face faded.
double jointAt(const RockFace& f, double y) {
    if (!(f.joint > 0.0f)) {
        return 0.0;
    }
    const double foot = f.foot;
    const double top = f.top;
    const std::array<double, 2> stretch = stretchAt(f, y);
    if (!(stretch[1] > stretch[0])) {
        return 0.0;
    }
    const double most = kJointEndShare * (top - foot);
    const double half = 0.5 * (stretch[1] - stretch[0]);
    const double depth = std::min({static_cast<double>(f.joint), kJointRoof * most / 1.5,
                                   kJointRoof * half / 1.5});
    if (!(depth > 0.0)) {
        return 0.0;
    }
    const double roof = std::max(kJointEnds, 1.5 * depth / kJointRoof);
    const double face = smooth(foot, foot + std::min(kJointEnds, most), y) *
                        (1.0 - smooth(top - std::min(roof, most), top, y));
    const double open = smooth(stretch[0], stretch[0] + std::min(kJointEnds, half), y) *
                        (1.0 - smooth(stretch[1] - std::min(roof, half), stretch[1], y));
    return depth * std::min(face, open);
}

// How far the face lies toward the air at height y of its step: its grain, its cap, full over the
// top two fifths of its depth and nothing at its bottom (a ceiling rising to the lip), less its
// undercut, an arch deepest at the foot, and its joint (jointAt); all of them at full weight
// only (kFullLow).
double offsetAt(const RockFace& f, double y, const RockDetail& detail) {
    const double full = smooth(kFullLow, 1.0, static_cast<double>(drawnWeight(f, detail)));
    if (!(full > 0.0)) {
        return 0.0;
    }
    // The grain leaves the rounded edges of the step alone (a bed standing out there would be a
    // blade of rock thinner than a cell).
    const double round = roundOf(f, detail);
    double offset = grainAt(f, y, detail) *
                    smooth(static_cast<double>(f.foot), static_cast<double>(f.foot) + round, y) *
                    (1.0 - smooth(static_cast<double>(f.top) - round, static_cast<double>(f.top), y));
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
    // A joint narrower than kJointCells cells fades out, gone at half that width (the cells would
    // catch its groove at one column out of two: a slit); a shallow one is drawn: it only moves
    // the face.
    offset -= jointAt(f, y) * smooth(0.5 * kJointCells * detail.cell, kJointCells * detail.cell,
                                     f.jointWidth);
    return full * offset;
}

// The density of the step of a face at height y: the rock below its top behind the face and below
// its foot in front of it, its edges rounded. Above its top or below its foot by reach (its
// rounding, its distance to the face and its cap: its offset there lies within its cap of 0, the
// grain, the undercut and the joint faded out), the rounding blends nothing (smoothMin and
// smoothMax are min and max: h = 0): the step is the plane of its top or of its foot there, the
// same value without its offset (most of the samples of a tile on a wall).
double stepDensity(const RockFace& f, double v, const RockDetail& detail) {
    const double round = roundOf(f, detail);
    const bool bevel = f.roundTenths > 0 && detail.cell > 1.0f; // reaches 1 + sqrt 2 its height
    const double reach = (bevel ? 2.5 : 1.0) * std::max(round, 0.0) +
                         std::abs(static_cast<double>(f.distance)) + std::abs(f.cap);
    if (v >= f.top + reach) {
        return v - f.top;
    }
    if (v <= f.foot - reach) {
        return v - f.foot;
    }
    const double across = static_cast<double>(f.distance) - offsetAt(f, v, detail);
    if (!(round > 0.0)) {
        return std::min(v - f.foot, std::max(v - f.top, across));
    }
    if (bevel) {
        const double cut = (v - f.top + across + round) * (1.0 / std::numbers::sqrt2);
        const double top = smoothMax(v - f.top, cut, kUpperCrease * round);
        return smoothMin(v - f.foot, smoothMax(top, across, kLowerCrease * round), round);
    }
    return smoothMin(v - f.foot, smoothMax(v - f.top, across, round), round);
}

// A face of weight below 1 leans toward the relief: its foot and top drawn to it by their weight
// (a lower step, flat at the relief without weight), its overhangs shallower. Blending its
// density with that of the relief instead would leave a ghost of the step in front of the face,
// a slit of air where its grain moves it.
RockFace leaned(const RockFace& f, double relief, double weight) {
    RockFace out = f;
    out.foot = static_cast<float>(relief + weight * (static_cast<double>(f.foot) - relief));
    out.top = static_cast<float>(relief + weight * (static_cast<double>(f.top) - relief));
    out.capDepth = static_cast<float>(weight * f.capDepth);
    out.undercutHeight = static_cast<float>(weight * f.undercutHeight);
    return out;
}

// The steps of the faces of one kind (carved or not) leaned on the relief by their weights,
// blended by them; the relief at height y (density y - relief) without them.
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
        steps += weight * stepDensity(leaned(f, relief, std::min(1.0, weight)), y, detail);
    }
    return weights > 0.0 ? steps / weights : y - relief;
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

namespace {

// The density of the relief of a column with its faces (rockDensity without its spire).
double facesDensity(const RockColumn& c, double y, const RockDetail& detail) {
    const bool drawn = std::any_of(c.faces.begin(), c.faces.begin() + c.faceCount,
                                   [&](const RockFace& f) {
                                       return drawnWeight(f, detail) > 0.0f;
                                   });
    if (!drawn) {
        return y - c.ground;
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
    return density;
}

// The highest the spire of a column stands (-infinity without one).
float spireSummit(const RockColumn& c) {
    return c.spire.radius > 0.0f
               ? static_cast<float>(rock_spires::summitOf(rock_spires::shapeOf(c.spire)))
               : -std::numeric_limits<float>::infinity();
}

} // namespace

float rockDensity(const RockColumn& c, double y, const RockDetail& detail) {
    const double density = facesDensity(c, y, detail);
    if (!(c.spire.radius > 0.0f)) {
        return static_cast<float>(density);
    }
    // The spire added to the relief (a union: the least of the two densities).
    const double spire =
        rock_spires::densityAt(rock_spires::shapeOf(c.spire), static_cast<double>(c.spire.dx),
                               static_cast<double>(c.spire.dz), y,
                               static_cast<double>(detail.cell));
    return static_cast<float>(std::min(density, spire));
}

float spireDensity(const RockSpire& spire, double dx, double dz, double y,
                   const RockDetail& detail) {
    return static_cast<float>(rock_spires::densityAt(rock_spires::shapeOf(spire), dx, dz, y,
                                                     static_cast<double>(detail.cell)));
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
    b.high = std::max(b.high, spireSummit(c));
    return b;
}

float rockCeiling(const RockColumn& c) {
    // rockDensity: rock where the relief before the rivers with its faces holds some, and below
    // that relief only where the valley with its carved faces does too (their tops, leaned on
    // their relief by their weights, never above the highest of the two).
    float faces = -std::numeric_limits<float>::infinity(); // the tops of the faces of the relief
    float valley = c.ground;                                // the valley and its carved faces
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        float& high = c.faces[i].carved ? valley : faces;
        high = std::max(high, c.faces[i].top);
    }
    return std::max({c.ground, std::min(c.uncarved, valley), faces, spireSummit(c)});
}

float rockShare(const RockColumn& c, double y, const RockDetail& detail) {
    float share = 0.0f;
    // On a spire (and kShareAcross meters around its surface): bare rock.
    if (c.spire.radius > 0.0f &&
        rock_spires::densityAt(rock_spires::shapeOf(c.spire), static_cast<double>(c.spire.dx),
                               static_cast<double>(c.spire.dz), y,
                               static_cast<double>(detail.cell)) < kShareAcross) {
        share = 1.0f;
    }
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        const RockFace& f = c.faces[i];
        const double distance = f.distance;
        // Behind the face over its undercut and the bevel of its top (no tooth of the ground).
        const double top = f.roundTenths > 0 ? (1.0 + kUpperCrease) * roundOf(f, detail) : 0.0;
        const double behind = std::max(0.0f, f.undercut) + kShareAcross + top;
        if (y >= f.foot - kShareBelow && y <= f.top + kShareAbove &&
            distance < std::max(0.0f, f.cap) + kShareAcross && distance > -behind) {
            share = std::max(share, std::min(1.0f, drawnWeight(f, detail)));
        }
    }
    return share;
}

namespace {

// The coordinate of t among the beds of the grain (bedStart): bed k from k to k + 1.
double bedCoordinate(double t) {
    auto k = static_cast<std::int64_t>(std::floor(t / kBedWave));
    if (t < bedStart(k)) {
        --k;
    } else if (t >= bedStart(k + 1)) {
        ++k;
    }
    return static_cast<double>(k) + (t - bedStart(k)) / (bedStart(k + 1) - bedStart(k));
}

RockBands faceBands(const RockFace& f, double y) {
    RockBands out;
    if (f.grain > 0.0f) {
        // Counted from a base that changes only every kStrataHeight meters of height (the same for
        // every column there: no line broken between two columns; small numbers for the half
        // floats of the meshes).
        const double base = std::round(std::floor(y / kStrataHeight) * kStrataHeight / kBedWave);
        out.strata = static_cast<float>(bedCoordinate(y + static_cast<double>(f.grainShift)) - base);
        out.layered = std::min(1.0f, f.grain / kLayeredGrain) * std::min(1.0f, f.weight);
    }
    if (f.undercut >= kBandLeast && f.undercutHeight > 0.0f) {
        out.notch = static_cast<float>(y - static_cast<double>(f.foot));
        out.notchHeight = f.undercutHeight;
    }
    if (f.cap >= kBandLeast && f.capDepth > 0.0f) {
        out.cap = static_cast<float>(y - static_cast<double>(f.top - f.capDepth));
        out.capHeight = f.capDepth;
    }
    return out;
}

} // namespace

RockBands rockBands(const RockColumn& c, double y) {
    if (c.spire.radius > 0.0f) {
        return rock_spires::bandsAt(rock_spires::shapeOf(c.spire), static_cast<double>(c.spire.dx),
                                    static_cast<double>(c.spire.dz), y);
    }
    const RockFace* strongest = nullptr;
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        if (strongest == nullptr || c.faces[i].weight > strongest->weight) {
            strongest = &c.faces[i];
        }
    }
    return strongest != nullptr ? faceBands(*strongest, y) : RockBands{};
}

} // namespace terrain
