#include "mountain_forms.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace terrain::mountain_forms {

namespace {

// The valleys follow the zero lines of their noises in a space warped by up to kWarp meters
// (2 octaves from kWarpWavelength): sinuous lines, unevenly spaced.
constexpr double kWarpWavelength = 60'000.0;
constexpr double kWarp = 7'000.0;
// Main valleys about half of kMainWavelength apart; tributaries about half of kSideWavelengthA
// or kSideWavelengthB apart, on either side of the main valley.
constexpr double kMainWavelength = 50'000.0;
constexpr double kSideWavelengthA = 11'000.0;
constexpr double kSideWavelengthB = 9'000.0;
// Distance to a zero line, first order: |n| / |grad n|, the gradient at least kLeastGradient
// per meter.
constexpr double kLeastGradient = 1.0e-7;

// A valley in U: a flat floor of half width w, then the massif comes down to it over the width
// of its walls (a smoothstep: rounded at their foot, a shoulder at their top), its crests and
// hollows kept: no plane. Main floors kMainHalfLow to kMainHalfHigh wide on each side of the axis
// with walls kMainWallLow to kMainWallHigh wide (a slow noise of kWidthWavelength); tributaries
// kSideHalf and kSideWall. The floor ends kBottom meters into the walls.
constexpr double kBottom = 300.0;
constexpr double kMainHalfLow = 500.0;
constexpr double kMainHalfHigh = 1'500.0;
constexpr double kMainWallLow = 1'800.0;
constexpr double kMainWallHigh = 3'200.0;
constexpr double kWidthWavelength = 22'000.0;
constexpr double kSideHalf = 350.0;
constexpr double kSideWall = 1'400.0;
// The main valley and a tributary meet rounded over up to kJoin meters.
constexpr double kJoin = 150.0;
// The floors of the main valleys climb to passes up to kPasses of the height of the mountains,
// along a slow noise of kPassWavelength (7 degrees at most). The main valleys are where a noise
// of kPresenceWavelength passes kPresenceLow to kPresenceHigh: elsewhere their floors rise
// kEnds of the height of the mountains, and they end in cirques and passes (no loop).
constexpr double kPasses = 0.3;
constexpr double kPassWavelength = 60'000.0;
constexpr double kPresenceWavelength = 50'000.0;
constexpr double kPresenceLow = -0.25;
constexpr double kPresenceHigh = 0.15;
constexpr double kEnds = 0.6;
// A point is on a floor where the valley cuts kCutLow to kCutHigh meters of the massif.
constexpr double kCutLow = 30.0;
constexpr double kCutHigh = 200.0;
// The tributaries are kept where the main noise is of their sign beyond kSideEdge (their own side
// of the main valley) and fade out from kReachLow to kReachHigh meters from it; their floors
// climb kClimb meters per meter away from it, and hang up to kHang meters above it where a slow
// noise of kHangWavelength passes kHangLow to kHangHigh.
constexpr double kSideEdge = 0.03;
constexpr double kReachLow = 4'000.0;
constexpr double kReachHigh = 9'000.0;
constexpr double kClimb = 0.06;
constexpr double kHang = 450.0;
constexpr double kHangWavelength = 30'000.0;
constexpr double kHangLow = 0.15;
constexpr double kHangHigh = 0.5;

// The summits: one in kSummitShare of the cells of about kSummitCell meters, within kSummitJitter
// of a cell from its center; the crests of the massif rise kProminenceLow to kProminenceHigh
// meters there (over kLevel of the height of the mountains), over kRadiusLow to kRadiusHigh
// meters around, as (1 - r / radius)^2: steepest at the center.
constexpr double kSummitCell = 16'384.0;
constexpr double kSummitShare = 0.6;
constexpr double kSummitJitter = 0.35;
constexpr double kLevel = 0.6;
constexpr double kProminenceLow = 1'200.0;
constexpr double kProminenceHigh = 2'200.0;
constexpr double kRadiusLow = 2'500.0;
constexpr double kRadiusHigh = 3'500.0;
// Around the rise, out to kMoatEnd times its radius, the massif is lower by up to kMoat: the
// summit stands above its neighbors.
constexpr double kMoatEnd = 2.2;
constexpr double kMoat = 0.4;
// Under each summit a base of kBase of its prominence rises as (1 - r / radius)^2: the summit
// stands even where its site falls in a hollow of the massif.
constexpr double kBase = 1.2;
// The summits rise at most to about kCeiling meters: lower, or none, where the ground and the
// massif are already high.
constexpr double kCeiling = 8'300.0;
// Each summit moves onto the nearest zero line of the first octave of the massif (its crest) by
// kCrestSteps steps of Newton, at most kCrestReach meters.
constexpr int kCrestSteps = 2;
constexpr double kCrestReach = 4'000.0;


noise::FbmOctaves octaves(std::uint64_t seed, std::uint64_t stream, int count, double wavelength,
                          const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                             {.octaves = count,
                              .frequency = 1.0 / wavelength,
                              .lattices = noise::OctaveLattices::RotatedFast},
                             world);
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Field k (16 bits) of a hash, in [0, 1).
double field(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (16 * k)) & 0xFFFFU) / 65536.0;
}

double fbm(const noise::FbmOctaves& n, double x, double z, noise::FbmOctaves::Memo* memo) {
    return static_cast<double>(memo != nullptr ? n.fbm2D(x, z, *memo) : n.fbm2D(x, z));
}

// A zero line of a noise of one octave near (x, z): the distance to it (first order) and the
// direction away from it.
struct Line {
    double distance = 0.0;
    double awayX = 1.0;
    double awayZ = 0.0;
};

Line lineOf(const noise::FbmOctaves& n, double x, double z, noise::FbmOctaves::Memo* memo) {
    const noise::FbmOctaves::RidgedSlope r =
        memo != nullptr ? n.ridged2DSlope(x, z, 1.0, 0.0, 1, *memo)
                        : n.ridged2DSlope(x, z, 1.0, 0.0, 1);
    const double g = std::max(dmath::hypot(r.headX, r.headZ), kLeastGradient);
    // The ridged signal 1 - |n| rises toward the line.
    return {(1.0 - static_cast<double>(r.head)) / g, -r.headX / g, -r.headZ / g};
}

// The massif brought down to a valley in U of floor floor, at e meters beyond the half width of
// the floor, its walls wall meters wide; the point on its floor where the valley cuts the massif
// (kCutLow to kCutHigh meters).
Carve valleyForm(const Form& massif, double floor, double e, double wall, const Line& line) {
    const double above = std::max(0.0, massif.height - floor);
    const double t = std::clamp(e / wall, 0.0, 1.0);
    const double keep = t * t * (3.0 - 2.0 * t);
    const double rise = above * 6.0 * t * (1.0 - t) / wall;
    const double kept = above > 0.0 ? keep : 0.0;
    return {{floor + above * keep, kept * massif.gradX + rise * line.awayX,
             kept * massif.gradZ + rise * line.awayZ},
            (1.0 - smooth(0.0, kBottom, e)) * smooth(kCutLow, kCutHigh, above)};
}

std::uint64_t cellIndex(int i, int j, int cellsX, int cellsZ) {
    const auto wi = static_cast<std::uint64_t>((i % cellsX + cellsX) % cellsX);
    const auto wj = static_cast<std::uint64_t>((j % cellsZ + cellsZ) % cellsZ);
    return wj * static_cast<std::uint64_t>(cellsX) + wi;
}

// The move from (x, z) onto the nearest crest of the massif: the zero line of the first octave
// of its noise, where 1 - |n| rises to 1.
std::array<double, 2> crestShift(const Noises& n, double x, double z) {
    std::array<double, 2> d{};
    for (int step = 0; step < kCrestSteps; ++step) {
        const noise::FbmOctaves::RidgedSlope r =
            n.crests.ridged2DSlope(x + d[0], z + d[1], 1.0, 0.0, 1);
        const double g2 = r.headX * r.headX + r.headZ * r.headZ;
        if (!(g2 > kLeastGradient * kLeastGradient)) {
            break;
        }
        const double toward = (1.0 - static_cast<double>(r.head)) / g2;
        d = {d[0] + toward * r.headX, d[1] + toward * r.headZ};
        const double length = dmath::hypot(d[0], d[1]);
        if (length > kCrestReach) {
            d = {d[0] * kCrestReach / length, d[1] * kCrestReach / length};
            break;
        }
    }
    return d;
}

// The summit of cell (i, j), its place in meters from the center of the cell (i0, j0); on its
// crest if onCrest (the move computed from the wrapped cell: the same from every cell).
Summit summitOf(const Noises& n, int i, int j, int i0, int j0, bool onCrest) {
    const std::uint64_t a = noise::deriveSeed(n.summits, cellIndex(i, j, n.cellsX, n.cellsZ));
    Summit s;
    if (!(field(a, 0) < kSummitShare)) {
        return s;
    }
    const double sizeX = n.world.x / n.cellsX;
    const double sizeZ = n.world.z / n.cellsZ;
    const double jitterX = kSummitJitter * (2.0 * field(a, 1) - 1.0);
    const double jitterZ = kSummitJitter * (2.0 * field(a, 2) - 1.0);
    s.x = (i - i0 + jitterX) * sizeX;
    s.z = (j - j0 + jitterZ) * sizeZ;
    s.prominence = kProminenceLow + (kProminenceHigh - kProminenceLow) * field(a, 3);
    const std::uint64_t b = noise::deriveSeed(a, 1);
    s.radius = kRadiusLow + (kRadiusHigh - kRadiusLow) * field(b, 0);
    if (onCrest) {
        const int wi = (i % n.cellsX + n.cellsX) % n.cellsX;
        const int wj = (j % n.cellsZ + n.cellsZ) % n.cellsZ;
        const std::array<double, 2> d = crestShift(n, (wi + 0.5 + jitterX) * sizeX - 0.5 * n.world.x,
                                                   (wj + 0.5 + jitterZ) * sizeZ - 0.5 * n.world.z);
        s.x += d[0];
        s.z += d[1];
    }
    return s;
}

// The rises of the summits around a point: the highest (two summits on one crest never stack),
// lowered by the product of their rings.
struct Lifts {
    Form rise{1.0, 0.0, 0.0};
    Form base;
    Form moat{1.0, 0.0, 0.0};
};

// Summit s seen at (dx, dz) from it, r away: within its radius, the factor 1 + gain (1 - (r /
// radius)^2)^2 of the massif (broad enough to lift the crests near the center) and the base
// base (1 - r / radius)^2; beyond, out to kMoatEnd radii, the factor 1 - depth 16 u^2 (1 - u)^2
// (a lowered ring). With their gradients.
void addSummit(const Summit& s, double dx, double dz, double r, double gain, double base,
               double depth, Lifts& lifts) {
    if (r < s.radius) {
        const double q = 1.0 - (r / s.radius) * (r / s.radius);
        const double factor = 1.0 + gain * q * q;
        if (factor > lifts.rise.height) {
            const double along = -4.0 * gain * q / (s.radius * s.radius);
            lifts.rise = {factor, along * dx, along * dz};
        }
        const double t = 1.0 - r / s.radius;
        if (base * t * t > lifts.base.height) {
            const double along = r > 0.0 ? -2.0 * base * t / (s.radius * r) : 0.0;
            lifts.base = {base * t * t, along * dx, along * dz};
        }
        return;
    }
    const double span = (kMoatEnd - 1.0) * s.radius;
    const double u = (r - s.radius) / span;
    const double factor = 1.0 - depth * 16.0 * u * u * (1.0 - u) * (1.0 - u);
    const double along = -depth * 32.0 * u * (1.0 - u) * (1.0 - 2.0 * u) / (span * r);
    Form& m = lifts.moat;
    m = {m.height * factor, m.gradX * factor + m.height * along * dx,
         m.gradZ * factor + m.height * along * dz};
}

} // namespace

Noises makeNoises(std::uint64_t seed, const WorldSize& world, noise::FbmOctaves crests) {
    Noises n;
    n.crests = std::move(crests);
    n.warpX = octaves(seed, 0, 2, kWarpWavelength, world);
    n.warpZ = octaves(seed, 1, 2, kWarpWavelength, world);
    n.main = octaves(seed, 2, 1, kMainWavelength, world);
    n.sideA = octaves(seed, 3, 1, kSideWavelengthA, world);
    n.sideB = octaves(seed, 4, 1, kSideWavelengthB, world);
    n.passes = octaves(seed, 5, 2, kPassWavelength, world);
    n.widths = octaves(seed, 6, 1, kWidthWavelength, world);
    n.hanging = octaves(seed, 7, 1, kHangWavelength, world);
    n.presence = octaves(seed, 11, 1, kPresenceWavelength, world);
    n.summits = noise::deriveSeed(seed, 9);
    n.cellsX = std::max(1, static_cast<int>(std::lround(world.x / kSummitCell)));
    n.cellsZ = std::max(1, static_cast<int>(std::lround(world.z / kSummitCell)));
    n.world = world;
    n.twinWarps = n.warpX.twinOf(n.warpZ);
    return n;
}

Carve valleys(const Noises& n, double x, double z, double height, const Form& massif,
             Memo* memo) {
    std::array<float, 2> warp{};
    if (n.twinWarps && memo != nullptr) {
        warp = n.warpX.fbm2DPair(n.warpZ, x, z, memo->warpX, memo->warpZ);
    } else if (n.twinWarps) {
        warp = n.warpX.fbm2DPair(n.warpZ, x, z);
    } else {
        warp = {memo != nullptr ? n.warpX.fbm2D(x, z, memo->warpX) : n.warpX.fbm2D(x, z),
                memo != nullptr ? n.warpZ.fbm2D(x, z, memo->warpZ) : n.warpZ.fbm2D(x, z)};
    }
    const double wx = x + kWarp * static_cast<double>(warp[0]);
    const double wz = z + kWarp * static_cast<double>(warp[1]);
    const Line main = lineOf(n.main, wx, wz, memo ? &memo->main : nullptr);
    const double side = fbm(n.main, wx, wz, memo ? &memo->main : nullptr);
    const double presence =
        smooth(kPresenceLow, kPresenceHigh, fbm(n.presence, x, z, memo ? &memo->presence : nullptr));
    const double passes =
        height * (kPasses * smooth(-0.1, 0.5, fbm(n.passes, x, z, memo ? &memo->passes : nullptr)) +
                  kEnds * (1.0 - presence));
    const double width =
        smooth(-0.5, 0.5, fbm(n.widths, x, z, memo ? &memo->widths : nullptr));
    const double half = kMainHalfLow + (kMainHalfHigh - kMainHalfLow) * width;
    const double wall = kMainWallLow + (kMainWallHigh - kMainWallLow) * width;
    Carve out = valleyForm(massif, passes, main.distance - half, wall, main);
    const double reach = main.distance - half;
    const double keep = presence * (1.0 - smooth(kReachLow, kReachHigh, main.distance));
    if (!(keep > 0.0)) {
        return out;
    }
    const double hang =
        kHang * smooth(kHangLow, kHangHigh, fbm(n.hanging, x, z, memo ? &memo->hanging : nullptr));
    const bool positive = side >= 0.0;
    const double own = keep * smooth(0.0, kSideEdge, std::abs(side));
    const noise::FbmOctaves& tributaries = positive ? n.sideA : n.sideB;
    const Line line = lineOf(tributaries, wx, wz,
                             memo ? (positive ? &memo->sideA : &memo->sideB) : nullptr);
    const double floor = passes + hang + kClimb * std::max(0.0, reach) + (1.0 - own) * 2.0 * height;
    const Carve tributary = valleyForm(massif, floor, line.distance - kSideHalf, kSideWall, line);
    // The lower of the two, their meeting rounded over kJoin meters (less near the base, never
    // below it): a continuous slope.
    const double a = out.form.height;
    const double b = tributary.form.height;
    const double join = std::min(kJoin, 4.0 * std::max(0.0, std::min(a, b)));
    const double overlap = std::max(join - std::abs(a - b), 0.0);
    const double share = join > 0.0 ? 0.5 * overlap / join : 0.0;
    const double toB = a < b ? share : 1.0 - share;
    out.form = {std::min(a, b) - (join > 0.0 ? overlap * overlap / (4.0 * join) : 0.0),
                (1.0 - toB) * out.form.gradX + toB * tributary.form.gradX,
                (1.0 - toB) * out.form.gradZ + toB * tributary.form.gradZ};
    out.floor = (1.0 - toB) * out.floor + toB * tributary.floor;
    return out;
}

Summit summitOfCell(const Noises& n, int i, int j) {
    Summit s = summitOf(n, i, j, 0, 0, true);
    s.x += 0.5 * n.world.x / n.cellsX - 0.5 * n.world.x;
    s.z += 0.5 * n.world.z / n.cellsZ - 0.5 * n.world.z;
    return s;
}

Rise summits(const Noises& n, double x, double z, double ground, double height, double core,
             Memo* memo) {
    Rise out;
    if (!(core > 0.0) || !(height > 0.0)) {
        return out;
    }
    const double sizeX = n.world.x / n.cellsX;
    const double sizeZ = n.world.z / n.cellsZ;
    const double cx = (x + 0.5 * n.world.x) / sizeX;
    const double cz = (z + 0.5 * n.world.z) / sizeZ;
    const auto i = static_cast<int>(std::floor(cx));
    const auto j = static_cast<int>(std::floor(cz));
    Memo local;
    Memo& m = memo != nullptr ? *memo : local;
    // Through a memo the summits of the 3 x 3 cells move onto their crests once per cell;
    // without one, only those that may reach the point.
    if (!m.valid || m.cellI != i || m.cellJ != j) {
        for (std::size_t k = 0; k < m.summits.size(); ++k) {
            m.summits[k] = summitOf(n, i + static_cast<int>(k % 3) - 1,
                                    j + static_cast<int>(k / 3) - 1, i, j, memo != nullptr);
        }
        m.valid = true;
        m.cellI = i;
        m.cellJ = j;
    }
    // The point in meters from the center of its cell.
    const double px = (cx - i - 0.5) * sizeX;
    const double pz = (cz - j - 0.5) * sizeZ;
    Lifts lifts;
    for (std::size_t k = 0; k < m.summits.size(); ++k) {
        Summit s = m.summits[k];
        if (!(s.prominence > 0.0)) {
            continue;
        }
        if (memo == nullptr) {
            if (!(dmath::hypot(px - s.x, pz - s.z) < kMoatEnd * s.radius + kCrestReach)) {
                continue;
            }
            s = summitOf(n, i + static_cast<int>(k % 3) - 1, j + static_cast<int>(k / 3) - 1, i,
                         j, true);
        }
        const double dx = px - s.x;
        const double dz = pz - s.z;
        const double r = dmath::hypot(dx, dz);
        if (!(r < kMoatEnd * s.radius)) {
            continue;
        }
        // Lower summits where the ground and the massif are already high: the top of the rise,
        // at most ground + height + core * prominence * (1 / kLevel + kBase), under kCeiling.
        const double prominence =
            std::clamp((kCeiling - ground - height) / (1.0 / kLevel + kBase), 0.0, s.prominence);
        addSummit(s, dx, dz, r, core * prominence / (kLevel * height), core * kBase * prominence,
                  core * kMoat, lifts);
    }
    // The highest rise, lowered by the rings: (F f)' = F' f + F f'.
    const Form& f = lifts.rise;
    const Form& g = lifts.moat;
    out.factor = {f.height * g.height, f.gradX * g.height + f.height * g.gradX,
                  f.gradZ * g.height + f.height * g.gradZ};
    out.base = lifts.base;
    return out;
}

} // namespace terrain::mountain_forms
