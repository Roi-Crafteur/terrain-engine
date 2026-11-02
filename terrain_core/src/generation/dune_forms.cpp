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
constexpr double kStarLow = 0.6;
constexpr double kStarSpread = 0.7;
constexpr double kStarMost = 300.0;
constexpr double kArmLow = 1.2;
constexpr double kArmSpread = 1.3;
constexpr double kArmTop = 0.6;
constexpr double kArmTopSpread = 0.35;
constexpr double kArmTurn = 0.6;
constexpr double kArmBend = 0.35;

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

double transverse(double t, double wavelength, double height, double gap, double nominal) {
    const double room = std::min(wavelength, nominal) * (1.0 - gap);
    height = std::min(height, room / (1.0 / kLeeTan + kBackPerHeight));
    if (!(height > 0.0)) {
        return 0.0;
    }
    const double back = backLength(wavelength, height, gap);
    const double lee = height - kLeeTan * t * wavelength;
    const double up = (1.0 - t) * wavelength;
    const double rise = up < back ? height * (1.0 - hermite01(up / back)) : 0.0;
    return std::max({0.0, lee, rise});
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
    const double top = std::min(b.height, half / kBarchanAspect) * b.presence;
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
    for (int back = -1; back <= 1; ++back) {
        const double along = (b.t + back) * b.wavelength - crest; // downwind of the crest
        const double h = along >= 0.0
                             ? ridge - lee * along
                             : ridge * (1.0 - hermite01(-along / (kBarchanBack * top)));
        best = std::max(best, h);
    }
    return best;
}

double seif(double offset, double height, double slip) {
    const double left = kLeeTan + (kGentleTan - kLeeTan) * slip;
    const double right = kGentleTan + (kLeeTan - kGentleTan) * slip;
    return std::max(0.0, height - (offset < 0.0 ? left : right) * std::abs(offset));
}

namespace {

// One star of the cell (i, j) (wrapped as wi, wj for its draws) at (x, z).
double starOf(const StarLattice& s, std::int64_t i, std::int64_t j, std::int64_t wi,
              std::int64_t wj, double x, double z) {
    const std::uint64_t h = cellHash(s.key, wi, wj);
    const double there = presence(uniform(h), s.occupancy);
    if (!(there > 0.0)) {
        return 0.0;
    }
    const double cx = (static_cast<double>(i) + 0.5 +
                       kStarJitter * (uniform(noise::deriveSeed(h, 1)) - 0.5)) * s.cell -
                      0.5 * s.periodX;
    const double cz = (static_cast<double>(j) + 0.5 +
                       kStarJitter * (uniform(noise::deriveSeed(h, 2)) - 0.5)) * s.cell -
                      0.5 * s.periodZ;
    const double ox = x - cx;
    const double oz = z - cz;
    const double peak =
        std::min(s.height * (kStarLow + kStarSpread * uniform(noise::deriveSeed(h, 3))), kStarMost) *
        there;
    const double radius = peak / kStarTan;
    const double dist = dmath::hypot(ox, oz);
    if (!(dist < radius * (kArmLow + kArmSpread) * (1.0 + kArmBend))) {
        return 0.0;
    }
    double best = peak - kStarTan * dist;
    const int arms = 2 + static_cast<int>(noise::deriveSeed(h, 4) % 5);
    const double turn = uniform(noise::deriveSeed(h, 5));
    for (int k = 0; k < arms; ++k) {
        const std::uint64_t ka = noise::deriveSeed(h, 10 + static_cast<std::uint64_t>(k));
        const dmath::SineCosine d = dmath::sinCosTurns(
            turn + (static_cast<double>(k) + kArmTurn * (uniform(ka) - 0.5)) / arms);
        const double along = ox * d.cosine + oz * d.sine;
        const double length = radius * (kArmLow + kArmSpread * uniform(noise::deriveSeed(ka, 1)));
        if (!(along > 0.0) || !(along < length)) {
            continue;
        }
        // The arm bends sideways along a parabola, its crest falling from the peak to its end.
        const double bend = kArmBend * (2.0 * uniform(noise::deriveSeed(ka, 2)) - 1.0);
        // Across the bent crest: the offset from the parabola over its slope, so that the flanks keep
        // the angle of their slip faces.
        const double tilt = 2.0 * bend * along / length;
        const double across = std::abs(oz * d.cosine - ox * d.sine - bend * along * along / length) /
                              std::sqrt(1.0 + tilt * tilt);
        const double top = (kArmTop + kArmTopSpread * uniform(noise::deriveSeed(ka, 3))) * peak;
        best = std::max(best, top * (1.0 - along / length) - kLeeTan * across);
    }
    return std::max(0.0, best);
}

} // namespace

double stars(const StarLattice& s, double x, double z) {
    if (!(s.occupancy > 0.0) || !(s.height > 0.0)) {
        return 0.0;
    }
    const auto nx = static_cast<std::int64_t>(std::floor(s.periodX / s.cell + 0.5));
    const auto nz = static_cast<std::int64_t>(std::floor(s.periodZ / s.cell + 0.5));
    const dmath::FloorIndex gi = dmath::floorIndex((x + 0.5 * s.periodX) / s.cell);
    const dmath::FloorIndex gj = dmath::floorIndex((z + 0.5 * s.periodZ) / s.cell);
    double best = 0.0;
    for (std::int64_t dj = -1; dj <= 1; ++dj) {
        for (std::int64_t di = -1; di <= 1; ++di) {
            const std::int64_t i = gi.index + di;
            const std::int64_t j = gj.index + dj;
            best = std::max(best, starOf(s, i, j, ((i % nx) + nx) % nx, ((j % nz) + nz) % nz, x, z));
        }
    }
    return best;
}

} // namespace terrain::dune_forms
