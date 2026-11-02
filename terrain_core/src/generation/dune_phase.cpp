// The phase of the crests of the dunes (dune_phase.hpp).
#include "dune_phase.hpp"

#include "dune_forms.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::dune_phase {

namespace {

constexpr double kPivotJitter = 0.5;  // of kPivot, across
constexpr double kAngleJitter = 0.01; // turns (about 4 degrees across)
constexpr double kLatticeWarp = 0.3;  // of kPivot: the warp of the weights of the pivots
// The spacing of the crests of a pivot: kSpacingBase + kSpacingPerMeter times the height of its
// dunes (from the potential of the field there), spread by kSpacingSpread times a slow noise,
// within kSpacingLeast and kSpacingMost.
constexpr double kHeightLow = 20.0;
constexpr double kHeightRange = 280.0;
constexpr double kSpacingBase = 1100.0;
constexpr double kSpacingPerMeter = 6.5;
constexpr double kSpacingSpread = 0.3;
constexpr double kSpacingLeast = 1000.0;
constexpr double kSpacingMost = 3000.0;
// The sinuosity of the crests (share of the spacing) and the drift of the phase (cycles).
constexpr double kSinuous = 0.3;
constexpr double kDrift = 0.9;
// The local wavelength stays within these shares of the spacing of the pivots.
constexpr double kShortest = 0.05;
constexpr double kLongest = 2.5;
constexpr double kTurn = 6.283185307179586; // radians per turn

double hermite01(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

Pivot pivotOf(const Ergs::Impl& m, std::int64_t i, std::int64_t j) {
    const auto nx = static_cast<std::int64_t>(std::floor(m.world.x / kPivot + 0.5));
    const auto nz = static_cast<std::int64_t>(std::floor(m.world.z / kPivot + 0.5));
    Pivot p;
    p.key = dune_forms::cellHash(m.seed, ((i % nx) + nx) % nx, ((j % nz) + nz) % nz);
    const auto draw = [&](std::uint64_t k) {
        return dune_forms::uniform(noise::deriveSeed(p.key, k));
    };
    p.cx = (static_cast<double>(i) + 0.5 + kPivotJitter * (draw(1) - 0.5)) * kPivot - 0.5 * m.world.x;
    p.cz = (static_cast<double>(j) + 0.5 + kPivotJitter * (draw(2) - 0.5)) * kPivot - 0.5 * m.world.z;
    const double wx = wrapCoordinate(p.cx, m.world.x);
    const double wz = wrapCoordinate(p.cz, m.world.z);
    const std::array<double, 2> wind = windOf(m, wx, wz);
    const dmath::SineCosine turn = dmath::sinCosTurns(kAngleJitter * (draw(3) - 0.5));
    p.dx = wind[0] * turn.cosine - wind[1] * turn.sine;
    p.dz = wind[0] * turn.sine + wind[1] * turn.cosine;
    // The spacing from the sand of the field at the pivot (not at the point: it must not vary
    // within the pattern of the pivot).
    const double sand =
        noise::smoothstep(0.35f, 1.1f, erg_field::potentialAt(m.field, wx, wz));
    const double height = kHeightLow + kHeightRange * sand * std::sqrt(sand);
    // A slow noise (not a draw of the pivot) spreads the spacings: the next pivots are alike, the
    // phasors beat slowly; within a pivot, the drift of the phase varies them.
    p.spacing = std::clamp((kSpacingBase + kSpacingPerMeter * height) *
                               (1.0 + kSpacingSpread * static_cast<double>(m.sand.fbm2D(wx, wz))),
                           kSpacingLeast, kSpacingMost);
    p.phase = draw(5);
    return p;
}

// The weights of the 4 pivots of p at the point pt of s.
std::array<double, 4> weightsAt(const Ergs::Impl& m, const Shared& s, const Pivots& p, int pt) {
    const auto k = static_cast<std::size_t>(pt);
    const double gx = (s.x[k] + 0.5 * m.world.x) / kPivot - 0.5 + kLatticeWarp * s.drift[k];
    const double gz = (s.z[k] + 0.5 * m.world.z) / kPivot - 0.5 + kLatticeWarp * s.sinuosity[k];
    const double tx = hermite01(gx - static_cast<double>(p.fi));
    const double tz = hermite01(gz - static_cast<double>(p.fj));
    return {(1.0 - tx) * (1.0 - tz), tx * (1.0 - tz), (1.0 - tx) * tz, tx * tz};
}

double wrapHalf(double turns) {
    return turns - std::floor(turns + 0.5);
}

} // namespace

Shared sharedAt(const Ergs::Impl& m, double x, double z) {
    Shared s;
    s.x = {x, x + kStep, x};
    s.z = {z, z, z + kStep};
    for (std::size_t k = 0; k < 3; ++k) {
        s.sinuosity[k] = static_cast<double>(m.sinuosity.fbm2D(s.x[k], s.z[k]));
        s.drift[k] = static_cast<double>(m.drift.fbm2D(s.x[k], s.z[k]));
    }
    return s;
}

Pivots pivotsAt(const Ergs::Impl& m, const Shared& s) {
    Pivots p;
    const double gx = (s.x[0] + 0.5 * m.world.x) / kPivot - 0.5 + kLatticeWarp * s.drift[0];
    const double gz = (s.z[0] + 0.5 * m.world.z) / kPivot - 0.5 + kLatticeWarp * s.sinuosity[0];
    p.fi = dmath::floorIndex(gx).index;
    p.fj = dmath::floorIndex(gz).index;
    for (std::size_t k = 0; k < 4; ++k) {
        p.pivots[k] = pivotOf(m, p.fi + static_cast<std::int64_t>(k % 2),
                              p.fj + static_cast<std::int64_t>(k / 2));
    }
    return p;
}

Phase phaseAt(const Ergs::Impl& m, const Shared& s, const Pivots& p, const Kind& kind) {
    std::array<double, 3> angle{};
    double modulus = 0.0;
    double nominal = 0.0;
    const dmath::SineCosine r = dmath::sinCosTurns(kind.turn);
    for (int pt = 0; pt < 3; ++pt) {
        const auto t = static_cast<std::size_t>(pt);
        const std::array<double, 4> w = weightsAt(m, s, p, pt);
        double re = 0.0;
        double im = 0.0;
        for (std::size_t k = 0; k < 4; ++k) {
            if (!(w[k] > 0.0)) {
                continue;
            }
            const Pivot& v = p.pivots[k];
            const double ax = kind.across ? -v.dz : v.dx;
            const double az = kind.across ? v.dx : v.dz;
            const double ux = ax * r.cosine - az * r.sine;
            const double uz = ax * r.sine + az * r.cosine;
            const double spacing = kind.fixedSpacing > 0.0 ? kind.fixedSpacing : kind.spacing * v.spacing;
            const double own = kind.salt == 0 ? v.phase
                                              : dune_forms::uniform(noise::deriveSeed(v.key, kind.salt));
            const double along = (s.x[t] - v.cx) * ux + (s.z[t] - v.cz) * uz;
            const double q = (along + kSinuous * spacing * s.sinuosity[t]) / spacing + own +
                             kind.drift * kDrift * s.drift[t];
            const dmath::SineCosine c = dmath::sinCosTurns(q);
            re += w[k] * c.cosine;
            im += w[k] * c.sine;
            if (pt == 0) {
                nominal += w[k] * spacing;
            }
        }
        angle[t] = dmath::atan2(im, re) / kTurn;
        if (pt == 0) {
            modulus = dmath::hypot(re, im);
        }
    }
    Phase out;
    out.fraction = angle[0] - std::floor(angle[0]);
    const double gx = wrapHalf(angle[1] - angle[0]) / kStep;
    const double gz = wrapHalf(angle[2] - angle[0]) / kStep;
    const double g = dmath::hypot(gx, gz);
    out.wavelength = g > 0.0 ? std::clamp(1.0 / g, kShortest * nominal, kLongest * nominal)
                             : kLongest * nominal;
    out.coherence = std::clamp(modulus, 0.0, 1.0);
    out.nominal = nominal;
    return out;
}

} // namespace terrain::dune_phase
