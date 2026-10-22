#include "plain_forms.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::plain_forms {

namespace {

// Independent streams of the seed of the forms of the plains. New streams go at the end.
enum Stream : std::uint64_t {
    kKindStream,
    kSwellsStream,
    kHillsStream,
    kMicroStream,
    kWarpXStream,
    kWarpZStream,
    kVallonsStream,
    kBranchesStream,
    kPresenceStream,
    kKnollsStream,
    kIsolatedStream,
};

// The kind of plain: a noise of kKindWavelength, flat below kKindLow, rolling above kKindHigh
// (about an eighth of the plains each); the forms grow as the kind to the power kRolling: most
// plains are well alive.
constexpr double kKindWavelength = 220'000.0;
constexpr float kKindLow = -0.3f;
constexpr float kKindHigh = 0.3f;
constexpr double kRolling = 0.6;
// The relief of relief_local keeps kFlatLocal of itself in the flattest plains, all of it from
// the kind kLocalFull on.
constexpr double kFlatLocal = 0.15;
constexpr double kLocalFull = 0.6;
// Swells: a noise of kSwellWavelength raised where it passes kSwellLow (the low land between them
// holds together) to kSwellHigh, kSwellHeight meters high in the most rolling plains (as the
// kind to the power 1.5).
constexpr double kSwellWavelength = 8'000.0;
constexpr double kSwellLow = 0.1;
constexpr double kSwellHigh = 0.6;
constexpr double kSwellHeight = 220.0;
// Hills of kHillsWavelength (kHillsOctaves) and micro relief of kMicroWavelength, from their flat
// to their rolling amplitude (meters, of the noise).
constexpr double kHillsWavelength = 1'100.0;
constexpr int kHillsOctaves = 3;
constexpr double kHillsFlat = 1.5;
constexpr double kHillsRolling = 100.0;
constexpr double kMicroWavelength = 80.0;
constexpr double kMicroFlat = 0.4;
constexpr double kMicroRolling = 6.0;
// The vallons and the hills are drawn in a space warped by up to kWarp meters (a noise of
// kWarpWavelength): wandering lines, irregular outlines.
constexpr double kWarp = 350.0;
constexpr double kWarpWavelength = 1'500.0;
// The vallons take kVallonWarp of the warp: they wander without looping.
constexpr double kVallonWarp = 0.4;
// Vallons: along the zero lines of a noise of one octave (kVallonWavelength), a trough
// kVallonHalfLow to kVallonHalfHigh meters wide on each side (the presence noise), kVallonDeepLow
// to kVallonDeepHigh meters deep from the flattest plains to the most rolling; faded out where
// the noise changes by less than kVallonFadeLow to kVallonFadeHigh per meter (near its small
// extrema, where its zero lines would close in rings). Branches along their own noise
// (kBranchWavelength) the same way, kBranchHalf wide, kBranchShare as deep, where the presence
// noise (kPresenceWavelength) passes kBranchLow to kBranchHigh. The presence also varies the
// depth along them (up to kVallonVary less); kVallonLands less outside the lands of vallons;
// kVallonOnSwells less on the swells (they run in the low land between them); none on the
// isolated hills (their gullies). They head on the crests of the hills: faded out where the
// noise of the hills passes kVallonCrestLow to kVallonCrestHigh (no line closes in a ring).
constexpr double kVallonWavelength = 2'400.0;
constexpr double kVallonHalfLow = 50.0;
constexpr double kVallonHalfHigh = 150.0;
constexpr double kVallonFadeLow = 1.2e-4;
constexpr double kVallonFadeHigh = 2.5e-4;
constexpr double kVallonDeepLow = 8.0;
constexpr double kVallonDeepHigh = 25.0;
constexpr double kBranchWavelength = 1'100.0;
constexpr double kBranchShare = 0.5;
constexpr double kBranchHalf = 60.0;
constexpr double kBranchFadeLow = 2.5e-4;
constexpr double kBranchFadeHigh = 5.0e-4;
constexpr double kLeastGradient = 1e-7;
constexpr double kPresenceWavelength = 5'000.0;
constexpr double kBranchLow = 0.0;
constexpr double kBranchHigh = 0.4;
constexpr double kVallonVary = 0.4;
constexpr double kVallonLands = 0.25;
constexpr double kVallonOnSwells = 0.4;
constexpr double kVallonCrestLow = 0.05;
constexpr double kVallonCrestHigh = 0.25;
// Isolated hills: in kHillShare of the cells of about kHillCell meters, within kHillJitter of a
// cell from its center, of radius kHillRadiusLow to kHillRadiusHigh meters, kHillHeightLow to
// kHillHeightHigh high. A short ridge of kSummitsLow to kSummitsHigh rounded summits along an
// axis drawn, spread over kSpineLow to kSpineHigh of the radius on each side of the center and
// kSummitSide across, each of kSummitRadiusLow to kSummitRadiusHigh of the radius, the highest
// one drawn, the others kSummitLow to kSummitHigh of its height, blended over kSummitBlend (their
// saddles); all leaning the same way (kHillSkewLow to kHillSkewHigh: the flank toward the lean
// that much longer); the flanks of a summit over the outer kHillFlank of its radius, a rounded top
// within; their height varied by kKnolls (a noise of kKnollsWavelength: knolls and shoulders);
// their hills (1 + kOnHill p) and micro relief (1 + kMicroOnHill p) times stronger, p their share
// of the height. The vallons of the plain end at their foot: faded out from kHillAround times the
// radius of the nearest summit in.
constexpr double kHillCell = 7'000.0;
constexpr double kHillShare = 0.12;
constexpr double kHillJitter = 0.3;
constexpr double kHillRadiusLow = 600.0;
constexpr double kHillRadiusHigh = 1'500.0;
constexpr double kHillHeightLow = 50.0;
constexpr double kHillHeightHigh = 150.0;
constexpr int kSummitsLow = 2;
constexpr int kSummitsHigh = 4;
constexpr double kSpineLow = 0.3;
constexpr double kSpineHigh = 0.9;
constexpr double kSummitSide = 0.25;
constexpr double kSummitRadiusLow = 0.55;
constexpr double kSummitRadiusHigh = 0.85;
constexpr double kSummitLow = 0.45;
constexpr double kSummitHigh = 0.9;
constexpr double kSummitBlend = 0.15;
constexpr double kHillSkewLow = 0.25;
constexpr double kHillSkewHigh = 0.5;
constexpr double kHillFlank = 0.6;
constexpr double kKnolls = 0.45;
constexpr double kKnollsWavelength = 700.0;
constexpr double kOnHill = 2.0;
constexpr double kMicroOnHill = 0.5;
constexpr double kHillAround = 1.3;
// Their gullies: kGulliesLow to kGulliesHigh around each hill, their angles wandering by up to
// kGullyWander of a turn (the knolls noise: they bend), V-shaped where the cosine of their angle
// passes kGullyEdge, up to kGullyDepth of the height of the hill deep, some fainter (a second
// harmonic), on the outer part of the flanks only (from the foot over kGullyFoot, fading out
// from kGullyHigh to kGullyTop of the way up the nearest summit): never near the tops, where they
// would meet.
constexpr int kGulliesLow = 4;
constexpr int kGulliesHigh = 8;
constexpr double kGullyWander = 0.06;
constexpr double kGullyEdge = 0.75;
constexpr double kGullyDepth = 0.4;
constexpr double kGullyFoot = 0.1;
constexpr double kGullyHigh = 0.3;
constexpr double kGullyTop = 0.5;
constexpr double kTwoPi = 6.283185307179586476925286766559;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double fbm(const noise::FbmOctaves& n, double x, double z, noise::FbmOctaves::Memo* memo) {
    return static_cast<double>(memo != nullptr ? n.fbm2D(x, z, *memo) : n.fbm2D(x, z));
}

// Field k (16 bits) of a hash, in [0, 1).
double field(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (16 * k)) & 0xFFFFU) / 65536.0;
}

std::uint64_t cellIndex(int i, int j, int cellsX, int cellsZ) {
    const auto wi = static_cast<std::uint64_t>((i % cellsX + cellsX) % cellsX);
    const auto wj = static_cast<std::uint64_t>((j % cellsZ + cellsZ) % cellsZ);
    return wj * static_cast<std::uint64_t>(cellsX) + wi;
}

// The warp at a point, meters.
struct Warp {
    double x = 0.0;
    double z = 0.0;
};

Warp warpAt(const Noises& n, double x, double z, Memos* memos) {
    return {kWarp * fbm(n.warpX, x, z, memos ? &memos->warpX : nullptr),
            kWarp * fbm(n.warpZ, x, z, memos ? &memos->warpZ : nullptr)};
}

// The isolated hills at a point: their height (weight 1, their gullies cut), the greatest share
// of its height that a hill reaches there, and how much the vallons of the plain are faded out
// there (1 from the foot of a hill in).
struct Hill {
    double height = 0.0;
    double share = 0.0;
    double around = 0.0;
};

// The larger of two heights in [0, 1], blended over kSummitBlend (or the smaller, if lower: none
// at the foot of either): a rounded saddle where they meet, at most a quarter of that above the
// larger.
double blendMax(double a, double b) {
    const double blend = std::min({kSummitBlend, a, b});
    if (!(blend > 0.0)) {
        return std::max(a, b);
    }
    const double h = std::max(blend - std::abs(a - b), 0.0) / blend;
    return std::max(a, b) + 0.25 * blend * h * h;
}

// The summits of an isolated hill at (u, v) meters from its center along its axis and across:
// their blended share of the height, and the least distance to one of them over its radius
// (stretched by the lean).
struct Summits {
    double share = 0.0;
    double rho = 2.0;
};

Summits summitsOf(std::uint64_t hash, std::uint64_t axis, double u, double v, double radius) {
    const int count =
        kSummitsLow + static_cast<int>((kSummitsHigh - kSummitsLow + 1) * field(axis, 2));
    const auto highest = static_cast<int>(count * field(axis, 3));
    const double spine = radius * (kSpineLow + (kSpineHigh - kSpineLow) * field(axis, 1));
    const double lean = kHillSkewLow + (kHillSkewHigh - kHillSkewLow) * field(hash, 3);
    const double leanX = dmath::cosTurns(field(hash, 2));
    const double leanZ = dmath::sinTurns(field(hash, 2));
    Summits out;
    for (int k = 0; k < count; ++k) {
        const std::uint64_t own = noise::deriveSeed(axis, static_cast<std::uint64_t>(k) + 1);
        const double along = spine * (2.0 * (k + field(own, 0)) / count - 1.0);
        const double du = u - along;
        const double dv = v - kSummitSide * radius * (2.0 * field(own, 1) - 1.0);
        const double d = dmath::hypot(du, dv);
        // The lean: a cosine of the angle from the direction drawn, 0 at the summit.
        const double toward = d > 0.0 ? (du * leanX + dv * leanZ) / d : 0.0;
        const double size =
            radius * (kSummitRadiusLow + (kSummitRadiusHigh - kSummitRadiusLow) * field(own, 2));
        const double rho = d / (size * (1.0 + lean * toward));
        out.rho = std::min(out.rho, rho);
        if (!(rho < 1.0)) {
            continue;
        }
        const double height =
            k == highest ? 1.0 : kSummitLow + (kSummitHigh - kSummitLow) * field(own, 3);
        out.share = blendMax(out.share, height * smooth(0.0, kHillFlank, 1.0 - rho));
    }
    out.share = std::min(out.share, 1.0);
    return out;
}

// One isolated hill, drawn from its hashes, at (dx, dz) meters from its center after the warp:
// its share of the height there less its gullies (shape), without them (share), and around;
// wander turns added to the angles of the gullies.
struct HillPart {
    double shape = 0.0;
    double share = 0.0;
    double around = 0.0;
};

HillPart hillShape(std::uint64_t hash, std::uint64_t more, double dx, double dz, double radius,
                   double wander) {
    const std::uint64_t axis = noise::deriveSeed(hash, 2);
    const double u = dx * dmath::cosTurns(field(axis, 0)) + dz * dmath::sinTurns(field(axis, 0));
    const double v = dz * dmath::cosTurns(field(axis, 0)) - dx * dmath::sinTurns(field(axis, 0));
    const Summits summits = summitsOf(hash, axis, u, v, radius);
    HillPart part;
    part.around = 1.0 - smooth(1.0, kHillAround, summits.rho);
    part.share = summits.share;
    part.shape = summits.share;
    const double up = 1.0 - summits.rho; // 0 at the foot of the nearest summit, 1 on it
    const double flanks =
        smooth(0.0, kGullyFoot, up) * (1.0 - smooth(kGullyHigh, kGullyTop, up));
    if (!(flanks > 0.0)) {
        return part;
    }
    const int count =
        kGulliesLow + static_cast<int>((kGulliesHigh - kGulliesLow + 1) * field(more, 3));
    const double angle = dmath::atan2(v, u) / kTwoPi + wander;
    const double gully =
        smooth(kGullyEdge, 1.0, dmath::cosTurns(count * angle - field(hash, 1)));
    const double faint = 0.5 + 0.5 * dmath::cosTurns((count + 3) * angle - field(more, 0));
    part.shape *= 1.0 - kGullyDepth * flanks * gully * faint;
    return part;
}

Hill hillsOf(const Noises& n, double x, double z, const Warp& warp, Memos* memos) {
    const double sizeX = n.world.x / n.cellsX;
    const double sizeZ = n.world.z / n.cellsZ;
    const double cx = (x + 0.5 * n.world.x) / sizeX;
    const double cz = (z + 0.5 * n.world.z) / sizeZ;
    const auto i = static_cast<int>(std::floor(cx));
    const auto j = static_cast<int>(std::floor(cz));
    // The point in meters from the center of its cell, warped.
    const double px = (cx - i - 0.5) * sizeX + warp.x;
    const double pz = (cz - j - 0.5) * sizeZ + warp.z;
    Hill out;
    double knolls = 0.0;
    bool near = false;
    for (int dj = -1; dj <= 1; ++dj) {
        for (int di = -1; di <= 1; ++di) {
            const std::uint64_t hash =
                noise::deriveSeed(n.hillSeed, cellIndex(i + di, j + dj, n.cellsX, n.cellsZ));
            if (!(field(hash, 0) < kHillShare)) {
                continue;
            }
            const std::uint64_t more = noise::deriveSeed(hash, 1);
            const double hx = (di + kHillJitter * (2.0 * field(hash, 1) - 1.0)) * sizeX;
            const double hz = (dj + kHillJitter * (2.0 * field(more, 0) - 1.0)) * sizeZ;
            const double radius =
                kHillRadiusLow + (kHillRadiusHigh - kHillRadiusLow) * field(more, 1);
            const double reach =
                radius * (kSpineHigh + kSummitSide +
                          kSummitRadiusHigh * (1.0 + kHillSkewHigh) * kHillAround);
            if (!(dmath::hypot(px - hx, pz - hz) < reach)) {
                continue;
            }
            if (!near) {
                knolls = fbm(n.knolls, x, z, memos ? &memos->knolls : nullptr);
                near = true;
            }
            const HillPart part =
                hillShape(hash, more, px - hx, pz - hz, radius, kGullyWander * knolls);
            out.height +=
                (kHillHeightLow + (kHillHeightHigh - kHillHeightLow) * field(more, 2)) *
                part.shape;
            out.share = std::max(out.share, part.share);
            out.around = std::max(out.around, part.around);
        }
    }
    out.height *= 1.0 + kKnolls * knolls;
    return out;
}

// A trough along a line: 1 on it, 0 from half meters away on.
double trough(double distance, double half) {
    const double t = distance / half;
    return t * t < 1.0 ? (1.0 - t * t) * (1.0 - t * t) : 0.0;
}

// The zero line of a noise of one octave near a point: how far, meters (its value over its
// gradient), and how fast the noise changes there, per meter.
struct Line {
    double distance = 0.0;
    double gradient = 0.0;
};

Line lineOf(const noise::FbmOctaves& n, double x, double z, noise::FbmOctaves::Memo* memo) {
    // The ridged head of one octave is 1 - |noise|, its gradient that of the noise but for the
    // sign.
    const noise::FbmOctaves::RidgedSlope r = memo != nullptr
                                                 ? n.ridged2DSlope(x, z, 1.0, 0.0, 1, *memo)
                                                 : n.ridged2DSlope(x, z, 1.0, 0.0, 1);
    const double gradient = dmath::hypot(r.headX, r.headZ);
    return {(1.0 - static_cast<double>(r.head)) / std::max(gradient, kLeastGradient), gradient};
}

// The depth of the vallons at a warped point, as deep as deep on the deepest lines.
double vallonsOf(const Noises& n, double wx, double wz, double deep, double crest,
                 Memos* memos) {
    deep *= 1.0 - smooth(kVallonCrestLow, kVallonCrestHigh, crest);
    if (!(deep > 0.0)) {
        return 0.0;
    }
    const double presence = fbm(n.presence, wx, wz, memos ? &memos->presence : nullptr);
    const double vary = 1.0 - kVallonVary * smooth(-0.5, 0.5, -presence);
    const double half =
        kVallonHalfLow + (kVallonHalfHigh - kVallonHalfLow) * smooth(-0.5, 0.5, presence);
    const Line main = lineOf(n.vallons, wx, wz, memos ? &memos->vallons : nullptr);
    const double mainDepth = trough(main.distance, half) *
                             smooth(kVallonFadeLow, kVallonFadeHigh, main.gradient);
    const double branching = smooth(kBranchLow, kBranchHigh, presence);
    double branchDepth = 0.0;
    if (branching > 0.0) {
        const Line branch = lineOf(n.branches, wx, wz, memos ? &memos->branches : nullptr);
        branchDepth = kBranchShare * branching * trough(branch.distance, kBranchHalf) *
                      smooth(kBranchFadeLow, kBranchFadeHigh, branch.gradient);
    }
    return deep * vary * std::max(mainDepth, branchDepth);
}

} // namespace

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    const auto layer = [seed, &world](Stream stream, int octaves, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = octaves,
                                  .frequency = 1.0 / wavelength,
                                  .gain = 0.5,
                                  .lattices = noise::OctaveLattices::RotatedFast},
                                 world);
    };
    Noises n;
    n.kind = layer(kKindStream, 2, kKindWavelength);
    n.swells = layer(kSwellsStream, 2, kSwellWavelength);
    n.hills = layer(kHillsStream, kHillsOctaves, kHillsWavelength);
    n.micro = layer(kMicroStream, 2, kMicroWavelength);
    n.warpX = layer(kWarpXStream, 2, kWarpWavelength);
    n.warpZ = layer(kWarpZStream, 2, kWarpWavelength);
    n.vallons = layer(kVallonsStream, 1, kVallonWavelength);
    n.branches = layer(kBranchesStream, 1, kBranchWavelength);
    n.presence = layer(kPresenceStream, 1, kPresenceWavelength);
    n.knolls = layer(kKnollsStream, 2, kKnollsWavelength);
    n.hillSeed = noise::deriveSeed(seed, kIsolatedStream);
    n.cellsX = std::max(1, static_cast<int>(std::lround(world.x / kHillCell)));
    n.cellsZ = std::max(1, static_cast<int>(std::lround(world.z / kHillCell)));
    n.world = world;
    return n;
}

float kindAt(const Noises& noises, double x, double z, Memos* memos) {
    return static_cast<float>(smooth(kKindLow, kKindHigh,
                                     fbm(noises.kind, x, z, memos ? &memos->kind : nullptr)));
}

double hillAt(const Noises& noises, double x, double z, Memos* memos) {
    return hillsOf(noises, x, z, warpAt(noises, x, z, memos), memos).height;
}

float relief(const Noises& n, double x, double z, const Ground& g, Memos* memos) {
    const double kind = dmath::pow(static_cast<double>(kindAt(n, x, z, memos)), kRolling);
    const Warp warp = warpAt(n, x, z, memos);
    const Hill hill = hillsOf(n, x, z, warp, memos);
    // The relief of relief_local, scaled by the kind of plain; none from the foot of the isolated
    // hills in, which have their own (its buttes would ring their flanks).
    const double local = static_cast<double>(g.local) * (1.0 - hill.around) *
                         (kFlatLocal + (1.0 - kFlatLocal) * smooth(0.0, kLocalFull, kind));
    const double swellShare = smooth(kSwellLow, kSwellHigh,
                                     fbm(n.swells, x, z, memos ? &memos->swells : nullptr));
    const double swells = kSwellHeight * kind * std::sqrt(kind) * swellShare;
    const double crest = fbm(n.hills, x, z, memos ? &memos->hills : nullptr);
    const double hills = (1.0 + kOnHill * hill.share) *
                         (kHillsFlat + (kHillsRolling - kHillsFlat) * kind) * crest;
    const double micro = (1.0 + kMicroOnHill * hill.share) *
                         (kMicroFlat + (kMicroRolling - kMicroFlat) * kind) *
                         fbm(n.micro, x, z, memos ? &memos->micro : nullptr);
    // The vallons: in the low land between the swells, not on the isolated hills.
    const double lands = smooth(-0.4, 0.4, static_cast<double>(g.region));
    const double deep = (kVallonDeepLow + (kVallonDeepHigh - kVallonDeepLow) * kind) *
                        (1.0 - kVallonLands + kVallonLands * lands) *
                        (1.0 - kVallonOnSwells * swellShare) * (1.0 - hill.around);
    const double vallons =
        vallonsOf(n, x + kVallonWarp * warp.x, z + kVallonWarp * warp.z, deep, crest, memos);
    const double plain = local + swells + hill.height + hills + micro - vallons;
    const double weight = static_cast<double>(g.weight);
    return static_cast<float>(static_cast<double>(g.local) +
                              weight * (plain - static_cast<double>(g.local)));
}

} // namespace terrain::plain_forms
