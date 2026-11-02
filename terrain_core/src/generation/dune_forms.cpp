// The forms of the dunes (dune_forms.hpp).
#include "dune_forms.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::dune_forms {

namespace {

// The back of a dune rises along a Hermite curve, whose steepest slope is 1.5 times its mean: a
// back of kBackPerHeight times the height keeps it below about 12 degrees.
constexpr double kBackPerHeight = 7.0;
// Barchans: kBarchanHalf of their cell wide on each side of their axis, at most kBarchanAspect
// times as wide (on each side) as high, their horns kHorn times their half width downwind of
// their crest, their back kBarchanBack times their height long. A star is on its cell where the
// occupancy passes its draw (within kPresenceSoft).
constexpr double kBarchanHalf = 0.26;
constexpr double kBarchanAspect = 4.5;
constexpr double kHorn = 0.9;
constexpr double kBarchanBack = 9.0;
constexpr double kBarchanLee = 0.6; // tan 31 degrees
constexpr double kPresenceSoft = 0.12;
// Stars: their peak within kStarJitter of the center of their cell, kStarLow to kStarLow +
// kStarSpread of the height (at most kStarMost meters), 2 to 6 arms at uneven angles (kArmTurn of
// their share of the turn), kArmLow to kArmLow + kArmSpread times the radius of the peak long,
// their crests from kArmTop to kArmTop + kArmTopSpread of the peak down to the floor at their end,
// each bent sideways by up to kArmBend of its length at its end: no two alike.
constexpr double kStarJitter = 0.6;
constexpr double kStarLow = 0.35;
constexpr double kStarSpread = 0.85;
// Their base an ellipse, up to kStarStretch times as long as wide, turned at random.
constexpr double kStarStretch = 1.2;
constexpr double kStarMost = 300.0;
constexpr double kArmLow = 1.2;
constexpr double kArmSpread = 1.3;
constexpr double kArmTop = 0.6;
constexpr double kArmTopSpread = 0.35;
constexpr double kArmTurn = 0.6;
constexpr double kArmBend = 0.35;

// The trough between two ridges reaches this share of the back of the next; its flanks are never
// steeper than kTroughTan (14 degrees).
constexpr double kTroughBack = 0.4;
constexpr double kTroughTan = 0.25;
constexpr double kTroughFall = 1.5396007178390020; // 8 / 3^1.5

double hermite01(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double presence(double draw, double occupancy) {
    return hermite01((occupancy - (draw - kPresenceSoft)) / (2.0 * kPresenceSoft));
}

} // namespace

double hermite(double t) {
    return hermite01(t);
}

std::uint64_t cellHash(std::uint64_t key, std::int64_t i, std::int64_t j) {
    return noise::deriveSeed(noise::deriveSeed(key, static_cast<std::uint64_t>(i)),
                             static_cast<std::uint64_t>(j));
}

double backLength(double wavelength, double height, double gap) {
    return std::max(0.0, wavelength * (1.0 - gap) - height / kLeeTan);
}

double trough(double d, double from, double to, double depth) {
    if (!(depth > 0.0) || !(to > from) || !(d > from) || !(d < to)) {
        return 0.0;
    }
    // Never steeper than kTroughTan: (1 - u^2)^2 falls at most 8 / 3^1.5 per unit of u.
    const double deepest = kTroughTan * 0.5 * (to - from) / kTroughFall;
    const double u = (2.0 * d - from - to) / (to - from);
    const double v = 1.0 - u * u;
    return std::min(depth, deepest) * v * v;
}

double transverse(double t, double wavelength, double height, double gap, double nominal,
                  double dig) {
    const double room = std::min(wavelength, nominal) * (1.0 - gap);
    height = std::min(height, room / (1.0 / kLeeTan + kBackPerHeight));
    if (!(height > 0.0)) {
        return 0.0;
    }
    const double back = backLength(wavelength, height, gap);
    const double lee = height - kLeeTan * t * wavelength;
    const double up = (1.0 - t) * wavelength;
    const double rise = up < back ? height * (1.0 - hermite01(up / back)) : 0.0;
    const double form = std::max({0.0, lee, rise});
    // The trough: from the foot of the slip face over the floor and the lower part of the back.
    const double foot = height / kLeeTan;
    const double start = wavelength - back; // of the back of the next ridge
    return form - trough(t * wavelength, foot, start + kTroughBack * back, dig * height);
}

double barchan(const Barchan& b) {
    if (!(b.presence > 0.0) || !(b.height > 0.0)) {
        return 0.0;
    }
    const double half = kBarchanHalf * b.cell;
    const double lateral = (b.across - 0.5) * b.cell / half;
    if (!(std::abs(lateral) < 1.0)) {
        return 0.0;
    }
    const double top = std::min(b.height, half / kBarchanAspect);
    const double ridge = top * (1.0 - lateral * lateral);
    const double crest = b.wavelength * b.crest + kHorn * half * lateral * lateral;
    // The crest bends downwind toward the horns and falls toward them: the slip face, ridge -
    // lee * along with along = t wavelength - crest(lateral), has the gradient (-lee, lee bend - r)
    // (along the wind, across it), bend the slope of the crest in plan, r that of the ridge across.
    // lee is the root that makes its norm kBarchanLee: a little under the angle of repose, the room
    // the changes of size and presence of the barchans take.
    const double bend = 2.0 * kHorn * lateral;
    const double r = 2.0 * top * lateral / half;
    const double a = 1.0 + bend * bend;
    const double disc = bend * bend * r * r - a * (r * r - kBarchanLee * kBarchanLee);
    const double lee = disc > 0.0 ? std::max(0.0, (bend * r + std::sqrt(disc)) / a) : 0.0;
    double best = 0.0;
    // The barchan of this row, the end of that of the row before and the back of that of the next.
    const int rows = b.single ? 0 : 1;
    for (int back = -rows; back <= rows; ++back) {
        const double along = (b.t + back) * b.wavelength - crest; // downwind of the crest
        const double h = along >= 0.0
                             ? ridge - lee * along
                             : ridge * (1.0 - hermite01(-along / (kBarchanBack * top)));
        best = std::max(best, h);
    }
    return best * b.presence;
}

double rounded(double c, double slope) {
    const double reach = 2.0 * kCrestRound / slope;
    return c < reach ? kCrestRound + 0.5 * slope * c * c / reach : slope * c;
}

double seif(double offset, double height, double slip) {
    const double left = kLeeTan + (kGentleTan - kLeeTan) * slip;
    const double right = kGentleTan + (kLeeTan - kGentleTan) * slip;
    return std::max(0.0, height - rounded(std::abs(offset), offset < 0.0 ? left : right));
}

namespace {

constexpr double kNone = -1.0e30;

// How far from its center a star of this peak reaches down to depth under its foot: its cone
// (stretched) or the end of its longest arm (bent), then down its flanks at kLeeTan (with the
// slack of the bend of the arm).
double reachOf(double peak, double depth) {
    const double radius = peak / kStarTan;
    const double cone = (peak + depth) / kStarTan * (1.0 + kStarStretch);
    if (!(depth > 0.0)) {
        return std::max(cone, radius * (kArmLow + kArmSpread) * (1.0 + kArmBend));
    }
    // The gentlest flank of an arm: under its steepest crest (armOf).
    const double fall = (kArmTop + kArmTopSpread) * kStarTan / kArmLow;
    const double flank = std::sqrt(kLeeTan * kLeeTan - fall * fall);
    const double arms = radius * (kArmLow + kArmSpread) * (1.0 + kArmBend) +
                        (peak + depth) / flank * std::sqrt(1.0 + 4.0 * kArmBend * kArmBend);
    return std::max(cone, arms);
}

// One arm of a star (its hash ka) at (ox, oz) from its center: its crest bends sideways along a
// parabola and falls from top at the peak to the foot at its end, then on along its axis; its
// flanks and its crest together at kLeeTan at most. Across the bent crest, the offset from the
// parabola over its slope, so that the flanks keep their angle.
double armOf(std::uint64_t ka, double turn, int k, int arms, double radius, double top,
             double ox, double oz) {
    const dmath::SineCosine d = dmath::sinCosTurns(
        turn + (static_cast<double>(k) + kArmTurn * (uniform(ka) - 0.5)) / arms);
    const double along = ox * d.cosine + oz * d.sine;
    if (!(along > 0.0)) {
        return kNone;
    }
    const double length = radius * (kArmLow + kArmSpread * uniform(noise::deriveSeed(ka, 1)));
    const double bend = kArmBend * (2.0 * uniform(noise::deriveSeed(ka, 2)) - 1.0);
    const double on = std::min(along, length);
    const double tilt = 2.0 * bend * on / length;
    const double across = std::abs(oz * d.cosine - ox * d.sine - bend * on * on / length) /
                          std::sqrt(1.0 + tilt * tilt);
    // The crest falls at top / length: the flanks at their angle with it.
    const double fall = top / length;
    const double flank = std::sqrt(std::max(0.0, kLeeTan * kLeeTan - fall * fall));
    if (along < length) {
        return top * (1.0 - along / length) - flank * across;
    }
    return -dmath::hypot(kLeeTan * (along - length), flank * across);
}

// One star of the cell (i, j) (wrapped as wi, wj for its draws) at (x, z).
double starOf(const StarLattice& s, std::int64_t i, std::int64_t j, std::int64_t wi,
              std::int64_t wj, double x, double z,
              const std::function<Pedestal(double, double)>* pedestal) {
    const double none = pedestal != nullptr ? kNone : 0.0;
    const std::uint64_t h = cellHash(s.key, wi, wj);
    const double there = presence(uniform(h), s.occupancy);
    if (!(there > 0.0)) {
        return none;
    }
    const double cx = (static_cast<double>(i) + 0.5 +
                       kStarJitter * (uniform(noise::deriveSeed(h, 1)) - 0.5)) * s.cell -
                      0.5 * s.periodX;
    const double cz = (static_cast<double>(j) + 0.5 +
                       kStarJitter * (uniform(noise::deriveSeed(h, 2)) - 0.5)) * s.cell -
                      0.5 * s.periodZ;
    const double ox = x - cx;
    const double oz = z - cz;
    double peak =
        std::min(s.height * (kStarLow + kStarSpread * uniform(noise::deriveSeed(h, 3))), kStarMost) *
        there;
    if (!(dmath::hypot(ox, oz) < reachOf(peak, s.depth))) {
        return none;
    }
    Pedestal under;
    double depth = s.depth;
    if (pedestal != nullptr) {
        under = (*pedestal)(cx, cz);
        peak = std::min(under.height * (kStarLow + kStarSpread * uniform(noise::deriveSeed(h, 3))),
                        kStarMost) *
               there * under.presence;
        depth = under.depth;
    }
    const double radius = peak / kStarTan;
    // The peak: a cone over an ellipse (its long axis stretched, its slopes gentler along it).
    const dmath::SineCosine e = dmath::sinCosTurns(uniform(noise::deriveSeed(h, 7)));
    const double stretch = 1.0 + kStarStretch * uniform(noise::deriveSeed(h, 6));
    const double dist = dmath::hypot((ox * e.cosine + oz * e.sine) / stretch,
                                     oz * e.cosine - ox * e.sine);
    double best = peak - kStarTan * dist;
    const int arms = 2 + static_cast<int>(noise::deriveSeed(h, 4) % 5);
    const double turn = uniform(noise::deriveSeed(h, 5));
    for (int k = 0; k < arms; ++k) {
        const std::uint64_t ka = noise::deriveSeed(h, 10 + static_cast<std::uint64_t>(k));
        const double top = (kArmTop + kArmTopSpread * uniform(noise::deriveSeed(ka, 3))) * peak;
        best = std::max(best, armOf(ka, turn, k, arms, radius, top, ox, oz));
    }
    if (pedestal == nullptr) {
        return std::max(0.0, best);
    }
    // A star fading off its pedestal sinks under it.
    return under.base + best - (1.0 - under.presence) * depth;
}

} // namespace

// The stars of the cells around (x, z) whose stars may reach it, the highest.
double starsAround(const StarLattice& s, double x, double z, double none,
                   const std::function<Pedestal(double, double)>* pedestal) {
    const auto nx = static_cast<std::int64_t>(std::floor(s.periodX / s.cell + 0.5));
    const auto nz = static_cast<std::int64_t>(std::floor(s.periodZ / s.cell + 0.5));
    const dmath::FloorIndex gi = dmath::floorIndex((x + 0.5 * s.periodX) / s.cell);
    const dmath::FloorIndex gj = dmath::floorIndex((z + 0.5 * s.periodZ) / s.cell);
    // A center lies at least (k - 1 + (1 - kStarJitter) / 2) cells from a point k cells away.
    const double reach = reachOf(std::min(s.height * (kStarLow + kStarSpread), kStarMost), s.depth);
    const auto span = static_cast<std::int64_t>(
        std::ceil(reach / s.cell + 0.5 + 0.5 * kStarJitter) - 1.0);
    double best = none;
    for (std::int64_t dj = -span; dj <= span; ++dj) {
        for (std::int64_t di = -span; di <= span; ++di) {
            const std::int64_t i = gi.index + di;
            const std::int64_t j = gj.index + dj;
            best = std::max(best, starOf(s, i, j, ((i % nx) + nx) % nx, ((j % nz) + nz) % nz, x, z,
                                         pedestal));
        }
    }
    return best;
}

double stars(const StarLattice& s, double x, double z) {
    if (!(s.occupancy > 0.0) || !(s.height > 0.0)) {
        return 0.0;
    }
    return starsAround(s, x, z, 0.0, nullptr);
}

double starsOn(const StarLattice& s, double x, double z,
               const std::function<Pedestal(double, double)>& pedestal) {
    if (!(s.occupancy > 0.0) || !(s.height > 0.0)) {
        return -1.0e30;
    }
    return starsAround(s, x, z, -1.0e30, &pedestal);
}

} // namespace terrain::dune_forms
