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
// their crest, their back kBarchanBack times their height long.
constexpr double kBarchanHalf = 0.26;
constexpr double kBarchanAspect = 4.5;
constexpr double kHorn = 0.9;
constexpr double kBarchanBack = 9.0;
constexpr double kBarchanLee = 0.6; // tan 31 degrees
// The gentlest its slip face may fall (barchan: its crest across it, r, under 2 / kBarchanAspect;
// lee^2 (1 + bend^2) >= kBarchanLee^2 - r^2 with bend under 2 kHorn): 0.2 and a margin.
constexpr double kBarchanLeast = 0.15;
// The trough between two ridges reaches this share of the back of the next; its flanks are never
// steeper than kTroughTan (14 degrees).
constexpr double kTroughBack = 0.4;
constexpr double kTroughTan = 0.25;
constexpr double kTroughFall = 1.5396007178390020; // 8 / 3^1.5

double hermite01(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
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
    const double half = kBarchanHalf * b.cell * std::min(b.width, 1.9);
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

bool barchanMayStand(const Barchan& b, double crestLow, double crestHigh) {
    if (!(b.presence > 0.0) || !(b.height > 0.0) || !(b.width > 0.0)) {
        return false;
    }
    const double half = kBarchanHalf * b.cell * std::min(b.width, 1.9);
    const double lateral = (b.across - 0.5) * b.cell / half;
    if (!(std::abs(lateral) < 1.0)) {
        return false;
    }
    // Its slip face falls at lee >= kBarchanLeast (the crest across it under kBarchanLee: r <
    // 2 / kBarchanAspect), its back kBarchanBack times its height long.
    const double top = std::min(b.height, half / kBarchanAspect);
    const double bend = kHorn * half * lateral * lateral;
    const int rows = b.single ? 0 : 1;
    for (int back = -rows; back <= rows; ++back) {
        const double at = (b.t + back) * b.wavelength - bend;
        if (at - b.wavelength * crestLow > -kBarchanBack * top &&
            at - b.wavelength * crestHigh < top / kBarchanLeast) {
            return true;
        }
    }
    return false;
}

double barchanWidthWithin(const Barchan& b, double from, double ahead, double behind,
                          double cosine, double sine) {
    const double c = std::max(cosine, 1.0e-6);
    const double crest = (b.wavelength * b.crest - from) * c;
    // Downwind: the tips of the horns, kHorn times the half width downwind of the crest and the
    // half width across (the slip face reaches less far: under half / kBarchanAspect /
    // kBarchanLee; the points of the crest between, less far too).
    const double down = (ahead - crest) / (kHorn * c + std::abs(sine));
    // Upwind: the back, kBarchanBack times its height behind the crest, and where the crest bends
    // toward the horns, sine^2 / (4 kHorn cosine) of the half width farther across the turn.
    const double avail = behind + crest;
    const double bend = sine * sine / (4.0 * kHorn * c);
    const double asWide = avail / (kBarchanBack / kBarchanAspect * c + bend); // top: half / aspect
    double up = asWide;
    if (asWide > kBarchanAspect * b.height) { // top: its height
        const double rest = avail - kBarchanBack * b.height * c;
        up = bend > 0.0 ? rest / bend : (rest >= 0.0 ? 1.0e30 : 0.0);
        up = std::max(up, kBarchanAspect * b.height);
    }
    return std::max(0.0, std::min(down, up) / (kBarchanHalf * b.cell));
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

} // namespace terrain::dune_forms
