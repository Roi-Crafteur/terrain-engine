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
// The spacing of the crests of a pivot: kSpacingLeast + kSpacingRange sand^kSpacingPower, spread
// by kSpacingSpread times a slow noise, within kSpacingLeast and kSpacingMost.
constexpr double kSpacingRange = 1900.0;
constexpr double kSpacingPower = 1.3;
constexpr double kSpacingSpread = 0.2;
constexpr double kSpacingLeast = 1000.0;
constexpr double kSpacingMost = 3000.0;
// The giant waves: their pivots stand 65 km apart, where the sand may differ a lot; their spacing
// comes from a noise of 700 km instead (the next pivots alike), kGiantLeast to kGiantLeast +
// kGiantRange meters (the noise sampled kGiantShift away from the turn of the wind it shares).
constexpr double kGiantLeast = 4'000.0;
constexpr double kGiantRange = 8'000.0;
constexpr double kGiantShift = 3'000'000.0;
// The big waves: kWavesLeast to kWavesRatio times as much, a median about 2 km.
constexpr double kWavesLeast = 1'500.0;
constexpr double kWavesRatio = 2.0;
constexpr double kWavesShift = 2'222'222.0;
// The sinuosity of the crests (share of the spacing) and the drift of the phase (cycles).
constexpr double kSinuous = 0.3;
constexpr double kDrift = 0.6;
// The local wavelength stays within these shares of the spacing of the pivots.
constexpr double kShortest = 0.05;
constexpr double kLongest = 2.5;
constexpr double kTurn = 6.283185307179586; // radians per turn

double hermite01(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

Pivot pivotOf(const Ergs::Impl& m, std::int64_t i, std::int64_t j, bool giant) {
    const double size = giant ? kGiantPivot : kPivot;
    const auto nx = static_cast<std::int64_t>(std::floor(m.world.x / size + 0.5));
    const auto nz = static_cast<std::int64_t>(std::floor(m.world.z / size + 0.5));
    Pivot p;
    const std::uint64_t key = giant ? noise::deriveSeed(m.seed, 0x4749414E54ULL) : m.seed;
    p.key = dune_forms::cellHash(key, ((i % nx) + nx) % nx, ((j % nz) + nz) % nz);
    const auto draw = [&](std::uint64_t k) {
        return dune_forms::uniform(noise::deriveSeed(p.key, k));
    };
    p.cx = (static_cast<double>(i) + 0.5 + kPivotJitter * (draw(1) - 0.5)) * size - 0.5 * m.world.x;
    p.cz = (static_cast<double>(j) + 0.5 + kPivotJitter * (draw(2) - 0.5)) * size - 0.5 * m.world.z;
    const double wx = wrapCoordinate(p.cx, m.world.x);
    const double wz = wrapCoordinate(p.cz, m.world.z);
    const std::array<double, 2> wind = windOf(m, wx, wz);
    const dmath::SineCosine turn = dmath::sinCosTurns(kAngleJitter * (draw(3) - 0.5));
    p.dx = wind[0] * turn.cosine - wind[1] * turn.sine;
    p.dz = wind[0] * turn.sine + wind[1] * turn.cosine;
    // The spacing from the sand at the pivot (not at the point: it must not vary within the
    // pattern of the pivot), as the sand of an erg point reads it without its mask: from the
    // barchans and small ridges of a thin erg to the draas of its heart. A slow noise spreads it
    // (the next pivots are alike, their phasors beat slowly).
    const double potential = erg_field::potentialAt(m.field, wx, wz);
    const double grain = 1.0 + 0.45 * static_cast<double>(m.sand.fbm2D(wx, wz));
    const double sand =
        noise::smoothstep(0.35f, 1.1f, static_cast<float>(potential * grain));
    const double spread = 1.0 + kSpacingSpread * static_cast<double>(m.variability.fbm2D(wx, wz));
    p.spacing = std::clamp(
        (kSpacingLeast + kSpacingRange * dmath::pow(sand, kSpacingPower)) * spread, kSpacingLeast,
        kSpacingMost);
    // The big waves: kWavesLeast to kWavesRatio times it, by the sand noise sampled elsewhere
    // (some 140 km across: the next pivots alike, no period).
    const float region = m.sand.fbm2D(wrapCoordinate(wx - kWavesShift, m.world.x),
                                      wrapCoordinate(wz + kWavesShift, m.world.z));
    p.waves = kWavesLeast * dmath::pow(kWavesRatio, static_cast<double>(
                                                        noise::smoothstep(-0.4f, 0.4f, region)));
    if (giant) {
        const float slow = m.turn.fbm2D(wrapCoordinate(wx + kGiantShift, m.world.x),
                                        wrapCoordinate(wz - kGiantShift, m.world.z));
        p.spacing = kGiantLeast + kGiantRange * static_cast<double>(noise::smoothstep(-0.4f, 0.4f, slow));
    }
    p.phase = draw(5);
    return p;
}

} // namespace

namespace {

// The warp of the lattice of the pivots, in cells: the same in meters on both lattices (the noises
// that warp it keep their wavelengths: a wider lattice warped as much in cells would tear).
double warpOf(const Pivots& p) {
    return kLatticeWarp * kPivot / p.size();
}

} // namespace

Shared sharedAt(const Ergs::Impl& m, double x, double z) {
    Shared s;
    s.x = {x, x + kStep, x};
    s.z = {z, z, z + kStep};
    ErgMemos& memo = memosOf(m);
    for (std::size_t k = 0; k < 3; ++k) {
        s.sinuosity[k] = static_cast<double>(m.sinuosity.fbm2D(s.x[k], s.z[k], memo.sinuosity));
        s.drift[k] = static_cast<double>(m.drift.fbm2D(s.x[k], s.z[k], memo.drift));
    }
    return s;
}

Pivots pivotsAt(const Ergs::Impl& m, const Shared& s, bool giant) {
    Pivots p;
    p.giant = giant;
    const double size = p.size();
    const double warp = warpOf(p);
    const double gx = (s.x[0] + 0.5 * m.world.x) / size - 0.5 + warp * s.drift[0];
    const double gz = (s.z[0] + 0.5 * m.world.z) / size - 0.5 + warp * s.sinuosity[0];
    p.fi = dmath::floorIndex(gx).index;
    p.fj = dmath::floorIndex(gz).index;
    // The pivots of the last cell of each lattice on this thread (a pure function of the cell:
    // the same bits).
    struct Last {
        std::uint64_t owner = 0;
        Pivots pivots;
    };
    thread_local std::array<Last, 2> lasts;
    Last& last = lasts[giant ? 1 : 0];
    if (last.owner == m.id && last.pivots.fi == p.fi && last.pivots.fj == p.fj) {
        return last.pivots;
    }
    for (std::size_t k = 0; k < 4; ++k) {
        p.pivots[k] = pivotOf(m, p.fi + static_cast<std::int64_t>(k % 2),
                              p.fj + static_cast<std::int64_t>(k / 2), giant);
    }
    last.owner = m.id;
    last.pivots = p;
    return p;
}

namespace {

// The weights of the 4 pivots at the point and their gradients per meter (the lattice warped by
// the drift and the sinuosity, Hermite between its nodes).
struct Weights {
    std::array<double, 4> w{};
    std::array<double, 4> gx{};
    std::array<double, 4> gz{};
};
Weights weightsWithSlopes(const Ergs::Impl& m, const Shared& s, const Pivots& p) {
    const double size = p.size();
    const double warp = warpOf(p);
    const double ux = (s.x[0] + 0.5 * m.world.x) / size - 0.5 + warp * s.drift[0] -
                      static_cast<double>(p.fi);
    const double uz = (s.z[0] + 0.5 * m.world.z) / size - 0.5 + warp * s.sinuosity[0] -
                      static_cast<double>(p.fj);
    // The lattice coordinates per meter: along x and z, and the warp.
    const double gxx = 1.0 / size + warp * (s.drift[1] - s.drift[0]) / kStep;
    const double gxz = warp * (s.drift[2] - s.drift[0]) / kStep;
    const double gzx = warp * (s.sinuosity[1] - s.sinuosity[0]) / kStep;
    const double gzz = 1.0 / size + warp * (s.sinuosity[2] - s.sinuosity[0]) / kStep;
    const double tx = hermite01(ux);
    const double tz = hermite01(uz);
    const double dtx = ux > 0.0 && ux < 1.0 ? 6.0 * ux * (1.0 - ux) : 0.0;
    const double dtz = uz > 0.0 && uz < 1.0 ? 6.0 * uz * (1.0 - uz) : 0.0;
    Weights out;
    const std::array<double, 4> ax = {1.0 - tx, tx, 1.0 - tx, tx};
    const std::array<double, 4> az = {1.0 - tz, 1.0 - tz, tz, tz};
    const std::array<double, 4> sx = {-1.0, 1.0, -1.0, 1.0};
    const std::array<double, 4> sz = {-1.0, -1.0, 1.0, 1.0};
    for (std::size_t k = 0; k < 4; ++k) {
        out.w[k] = ax[k] * az[k];
        // d(ax az) = sx dtx az grad(ux) + ax sz dtz grad(uz).
        out.gx[k] = sx[k] * dtx * az[k] * gxx + ax[k] * sz[k] * dtz * gzx;
        out.gz[k] = sx[k] * dtx * az[k] * gxz + ax[k] * sz[k] * dtz * gzz;
    }
    return out;
}

} // namespace

double nominalAt(const Ergs::Impl& m, const Shared& s, const Pivots& p, bool waves) {
    const Weights w = weightsWithSlopes(m, s, p);
    double nominal = 0.0;
    for (std::size_t k = 0; k < 4; ++k) {
        nominal += w.w[k] * (waves ? p.pivots[k].waves : p.pivots[k].spacing);
    }
    return nominal;
}

namespace {

// The phase of a kind from the weights of the pivots at the point.
Phase phaseOf(const Shared& s, const Pivots& p, const Weights& w, const Kind& kind) {
    const dmath::SineCosine r = dmath::sinCosTurns(kind.turn);
    // The phasor Z = sum w exp(2 pi i q) and its gradient, both parts.
    double re = 0.0;
    double im = 0.0;
    double reX = 0.0;
    double reZ = 0.0;
    double imX = 0.0;
    double imZ = 0.0;
    double nominal = 0.0;
    const double sinX = (s.sinuosity[1] - s.sinuosity[0]) / kStep;
    const double sinZ = (s.sinuosity[2] - s.sinuosity[0]) / kStep;
    const double driftX = kind.drift * kDrift * (s.drift[1] - s.drift[0]) / kStep;
    const double driftZ = kind.drift * kDrift * (s.drift[2] - s.drift[0]) / kStep;
    for (std::size_t k = 0; k < 4; ++k) {
        if (!(w.w[k] > 0.0) && w.gx[k] == 0.0 && w.gz[k] == 0.0) {
            continue;
        }
        const Pivot& v = p.pivots[k];
        const double ax = kind.across ? -v.dz : v.dx;
        const double az = kind.across ? v.dx : v.dz;
        const double ux = ax * r.cosine - az * r.sine;
        const double uz = ax * r.sine + az * r.cosine;
        const double spacing =
            kind.fixedSpacing > 0.0 ? kind.fixedSpacing
                                    : kind.spacing * (kind.waves ? v.waves : v.spacing);
        const double own =
            kind.salt == 0 ? v.phase : dune_forms::uniform(noise::deriveSeed(v.key, kind.salt));
        const double along = (s.x[0] - v.cx) * ux + (s.z[0] - v.cz) * uz;
        const double q = (along + kind.sinuous * kSinuous * spacing * s.sinuosity[0]) / spacing + own +
                         kind.drift * kDrift * s.drift[0];
        const double qx = kTurn * ((ux + kind.sinuous * kSinuous * spacing * sinX) / spacing + driftX);
        const double qz = kTurn * ((uz + kind.sinuous * kSinuous * spacing * sinZ) / spacing + driftZ);
        const dmath::SineCosine c = dmath::sinCosTurns(q);
        re += w.w[k] * c.cosine;
        im += w.w[k] * c.sine;
        reX += w.gx[k] * c.cosine - w.w[k] * qx * c.sine;
        reZ += w.gz[k] * c.cosine - w.w[k] * qz * c.sine;
        imX += w.gx[k] * c.sine + w.w[k] * qx * c.cosine;
        imZ += w.gz[k] * c.sine + w.w[k] * qz * c.cosine;
        nominal += w.w[k] * spacing;
    }
    const double modulus2 = re * re + im * im;
    const double angle = dmath::atan2(im, re) / kTurn;
    Phase out;
    out.fraction = angle - std::floor(angle);
    // The gradient of the argument of Z, in cycles per meter.
    const double gx = modulus2 > 0.0 ? (re * imX - im * reX) / modulus2 / kTurn : 0.0;
    const double gz = modulus2 > 0.0 ? (re * imZ - im * reZ) / modulus2 / kTurn : 0.0;
    const double g = dmath::hypot(gx, gz);
    out.wavelength = g > 0.0 ? std::clamp(1.0 / g, kShortest * nominal, kLongest * nominal)
                             : kLongest * nominal;
    out.local = g > 0.0 ? 1.0 / g : out.wavelength;
    out.coherence = std::clamp(std::sqrt(modulus2), 0.0, 1.0);
    out.nominal = nominal;
    return out;
}

} // namespace

Phase phaseAt(const Ergs::Impl& m, const Shared& s, const Pivots& p, const Kind& kind) {
    return phaseOf(s, p, weightsWithSlopes(m, s, p), kind);
}

std::array<Phase, 2> phasesAt(const Ergs::Impl& m, const Shared& s, const Pivots& p,
                              const Kind& first, const Kind& second) {
    const Weights w = weightsWithSlopes(m, s, p);
    return {phaseOf(s, p, w, first), phaseOf(s, p, w, second)};
}

} // namespace terrain::dune_phase
