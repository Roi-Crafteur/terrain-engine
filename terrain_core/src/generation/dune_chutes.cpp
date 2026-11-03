// The chutes of the slip faces of the giant waves (dune_chutes.hpp).
#include "dune_chutes.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace terrain::dune_chutes {

namespace {

// Down the straight slip face of the giant waves, the chutes of the avalanches (grain flows):
// gullies one by one, in cells kChuteRow wide across it, kChuteNone of them empty (about one every
// 2 kChuteRow, at random places: a comb of cosines of one spacing, starting and ending alike, drew
// rows of light blocks seen from 1 km, f9c3391), each its own place in its cell, width (kChuteWide
// to kChuteWide + kChuteWider), depth (kChuteSlope of its width at most, kChuteDeep of its run:
// never steeper across than 17 degrees, nor down it by more than 8), start (from the brink to
// kChuteStart of the run) and end (kChuteEnd to the whole run), faded in over kChuteIn and out over
// kChuteOut of the run; where they meet, the power mean of order 4 of their depths (the deeper
// one, smoothly: its slope 1.19 times theirs at most).
constexpr double kChuteRow = 30.0;
constexpr double kChuteNone = 0.5;
constexpr double kChuteWide = 9.0;
constexpr double kChuteWider = 16.0;
constexpr double kChuteSlope = 0.15;
constexpr double kChuteDeep = 0.015;
constexpr double kChuteStart = 0.3;
constexpr double kChuteEnd = 0.6;
constexpr double kChuteIn = 0.45;
constexpr double kChuteOut = 0.3;
constexpr double kChuteCell = 4'096.0;
// The far horizon draws them from kChuteNear to kChuteFar of the side of its samples less and less
// (a grid of 16 m or more would draw rows of spots, a checkerboard, from gullies 40 to 80 m
// apart): all of them on the tiles and on its first level (the same bits), none from its second
// (beyond 1 km), the festoons alone.
constexpr double kChuteNear = 8.0;
constexpr double kChuteFar = 16.0;
// The lattice of the festoons (festoons): it divides the world.
constexpr double kFestoonCell = 4'096.0;
constexpr double kPi = 3.14159265358979323846;
// The festoons and spurs of the giant waves (crescentic slip faces, as on the megadunes of the
// Badain Jaran and the Rub al-Khali): their crest and slip face shifted downwind and back by
// kFestoonSlope times the spacing over 2 pi, about kFestoonSpacing apart along the crest (300 to
// 600 m): the face turns by up to 24 degrees from one festoon to the next, its slope at most
// 1.1 times steeper.
constexpr double kFestoonSpacing = 450.0;
constexpr double kFestoonSlope = 0.45;

// smoothstep with a continuous curvature, [0, 1].
double smoother(double e0, double e1, double v) {
    const double t = std::clamp((v - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * t * (t * (6.0 * t - 15.0) + 10.0);
}

// A number in [0, 1) from the byte k of h.
double byteOf(std::uint64_t h, int k) {
    return static_cast<double>((h >> (8 * k)) & 0xffU) / 256.0;
}

// The depth of the chutes of one row (the cell of the lattice cell) a meters across the wind,
// d meters down a slip face run meters long.
double chuteRow(std::uint64_t cell, double a, double d, double run) {
    const double j0 = std::floor(a / kChuteRow);
    double depth = 0.0; // the sum of the fourth powers
    for (double j = j0 - 1.0; j <= j0 + 1.0; j += 1.0) {
        const auto row = static_cast<std::uint64_t>(static_cast<std::int64_t>(j));
        const std::uint64_t h = noise::detail::mix64(cell ^ (row * 0x9E3779B97F4A7C15ULL));
        if (byteOf(h, 0) < kChuteNone) {
            continue;
        }
        const double width = kChuteWide + kChuteWider * byteOf(h, 2);
        const double t = (a - (j + byteOf(h, 1)) * kChuteRow) / width;
        if (!(std::abs(t) < 1.0)) {
            continue;
        }
        const double start = kChuteStart * run * byteOf(h, 4);
        const double end = run * (kChuteEnd + (1.0 - kChuteEnd) * byteOf(h, 5));
        const double along = smoother(start, start + kChuteIn * run, d) *
                             (1.0 - smoother(end - kChuteOut * run, end, d));
        const double most = std::min(kChuteSlope * width, kChuteDeep * run);
        const double one = most * (0.4 + 0.6 * byteOf(h, 3)) * along * 0.5 *
                           (1.0 + dmath::sinCosTurns(0.5 * t).cosine);
        depth += one * one * one * one;
    }
    return std::sqrt(std::sqrt(depth));
}

// Waves across the wind at (x, z), about spacing meters apart: from the corners of a lattice of
// kFestoonCell (dividing the world: no seam where it wraps), each its own offset and spacing (0.7
// to 1.3 times spacing, from its indices modulo the cells of the world, and salt), blended
// bilinearly (the blend wanders their amplitude too: irregular, not a comb). The sum of the
// weights times the cosines, each by its spacing over 2 pi if bySpacing (then the slope of the
// sum across the wind is at most 1).
double acrossWaves(const Ergs::Impl& m, const dune_phase::Shared& s, double x, double z,
                   double spacing, double salt, bool bySpacing) {
    const std::array<double, 2> wind = windOf(m, x, z);
    const double u = std::floor(x / kFestoonCell);
    const double v = std::floor(z / kFestoonCell);
    const double fu = x / kFestoonCell - u;
    const double fv = z / kFestoonCell - v;
    const double nu = m.world.x / kFestoonCell;
    const double nv = m.world.z / kFestoonCell;
    double c = 0.0;
    for (int k = 0; k < 4; ++k) {
        const double dx = wrapCoordinate(x - (u + (k & 1)) * kFestoonCell, m.world.x);
        const double dz = wrapCoordinate(z - (v + (k >> 1)) * kFestoonCell, m.world.z);
        const double w = ((k & 1) != 0 ? fu : 1.0 - fu) * ((k >> 1) != 0 ? fv : 1.0 - fv);
        const double iu = std::fmod(std::fmod(u + (k & 1), nu) + nu, nu);
        const double iv = std::fmod(std::fmod(v + (k >> 1), nv) + nv, nv);
        const double offset = 0.37 * iu + 0.61 * iv + 0.123 * salt;
        const double h = 0.53 * iu + 0.29 * iv + 0.311 * salt;
        const double own = spacing * (0.7 + 0.6 * (h - std::floor(h)));
        const double turns = (dz * wind[0] - dx * wind[1]) / own + offset + 2.0 * s.drift[0];
        c += w * dmath::sinCosTurns(turns).cosine * (bySpacing ? own / (2.0 * kPi) : 1.0);
    }
    return c;
}

} // namespace

double festoons(const Ergs::Impl& m, const dune_phase::Shared& s, double x, double z) {
    return kFestoonSlope * acrossWaves(m, s, x, z, kFestoonSpacing, 1.0, true);
}

// The rows of the 4 corners of a lattice of kChuteCell (dividing the world: no seam where it
// wraps), each its own chutes, blended bilinearly.
double depth(const Ergs::Impl& m, const dune_phase::Shared& s, double x, double z, double d,
             double run) {
    const double near = 1.0 - smoother(kChuteNear, kChuteFar, s.footprint);
    if (!(d > 0.0) || !(d < run) || !(near > 0.0)) {
        return 0.0;
    }
    const std::array<double, 2> wind = windOf(m, x, z);
    const double u = std::floor(x / kChuteCell);
    const double v = std::floor(z / kChuteCell);
    const double fu = x / kChuteCell - u;
    const double fv = z / kChuteCell - v;
    const double nu = m.world.x / kChuteCell;
    const double nv = m.world.z / kChuteCell;
    double depth = 0.0;
    for (int k = 0; k < 4; ++k) {
        const double w = ((k & 1) != 0 ? fu : 1.0 - fu) * ((k >> 1) != 0 ? fv : 1.0 - fv);
        if (!(w > 0.0)) {
            continue;
        }
        const double dx = wrapCoordinate(x - (u + (k & 1)) * kChuteCell, m.world.x);
        const double dz = wrapCoordinate(z - (v + (k >> 1)) * kChuteCell, m.world.z);
        const double iu = std::fmod(std::fmod(u + (k & 1), nu) + nu, nu);
        const double iv = std::fmod(std::fmod(v + (k >> 1), nv) + nv, nv);
        const std::uint64_t cell = noise::detail::mix64(
            (static_cast<std::uint64_t>(iu) << 32) ^ static_cast<std::uint64_t>(iv) ^ 0xC4A7E5ULL);
        const double a = dz * wind[0] - dx * wind[1] + 4.0 * s.drift[0] * kChuteRow;
        depth += w * chuteRow(cell, a, d, run);
    }
    return near * depth;
}

} // namespace terrain::dune_chutes
