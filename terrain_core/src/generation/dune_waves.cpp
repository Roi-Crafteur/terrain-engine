// The waves of the ergs of the characters big waves and giant waves (dune_waves.hpp).
#include "dune_waves.hpp"

#include "dune_forms.hpp"
#include "dune_layers.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::dune_waves {

namespace {

// The face downwind of a crest: H (1 - d / L)^kLeeCurve over L = kLeeCurve H / tan 33 (its slope
// at the brink tan 33 degrees).
constexpr double kLeeCurve = 2.0;
// The back: H (1 - u / B)^kBackCurve from the crest (u = 0) to the trough (u = B): steepest at
// the crest, kBackCurve H / B, flat only at the trough where it meets the face (no floor).
constexpr double kBackCurve = 1.4;
constexpr double kWavesBackTan = 0.3249196962329063;   // tan 18 degrees
constexpr double kGiantsBackTan = 0.28674538575880794; // tan 16 degrees
// The big waves keep their own spacing at the pivots (dune_phase: 1.5 to 3 km, about 2 km), their
// far filter starting kWavesFilterScale times as far as that of the realistic ridges.
constexpr double kWavesFilterScale = 1.5;
constexpr double kGiantsSpacing = 0.75; // of the spacing their pivots hold (dune_phase)
constexpr double kGiantsFilterScale = 6.5;
// The noises of the sinuosity (5 km) and of the drift (12 km) keep their wavelengths: the wider
// the waves, the less of them they take, or their crests would scatter.
constexpr double kWavesSinuous = 1.4;
constexpr double kWavesDrift = 0.5;
constexpr double kGiantsSinuous = 0.35;
constexpr double kGiantsDrift = 0.15;
// Heights: big waves kWavesLow + kWavesRange sand^1.5, at least kWavesFill of what their spacing
// leaves room for (their backs steep, no flat floor where they stand far apart), within kWavesLow
// and kWavesHigh; giant
// waves kGiantsShare of their spacing (within kGiantsLow and kGiantsHigh); both times the noise
// of the heights.
constexpr double kWavesLow = 80.0;
constexpr double kWavesRange = 220.0;
constexpr double kWavesHigh = 300.0;
constexpr double kWavesFill = 0.9;
constexpr double kGiantsShare = 0.1;
constexpr double kGiantsLow = 300.0;
constexpr double kGiantsHigh = 760.0; // with their troughs, about 1,050 m from crest to trough
constexpr double kAmpBase = 0.92;
constexpr double kAmpRange = 0.25;
// The second system: its spacing over the first, its turn, where it lies (the noise of the edges
// of the ergs, 70 km, sampled kJoinShift away: a faster one would raise the second system in
// patches whose edges, at these heights, would be cliffs) and how much.
constexpr double kJoinSpacing = 1.6;
constexpr double kJoinTurn = 0.07; // turns: 25 degrees
constexpr double kJoinShift = 919'191.0;
constexpr float kJoinLow = 0.0f;
constexpr float kJoinHigh = 0.45f;
// The second system rises over the first only near its own crests, from kJoinTopLow to
// kJoinTopHigh of its height (its long backs over the troughs of the first drew pale sheets,
// turned 25 degrees, across them), and joins it smoothly over kJoinSoft of its height (a plain
// maximum drew a straight fold where they meet).
constexpr double kJoinTopLow = 0.2;
constexpr double kJoinTopHigh = 0.9;
constexpr double kJoinSoft = 0.15;
// The joins below the crests of the first system: kJoinHeight of the height of their own, a dome
// over their profile (a smoothstep of it, flat on its crest: a sharp crest barely over the first
// system drew a fine straight line; never steeper than their face), with no dunes laid on them. Their crests
// wander kJoinSinuous times as much as those of the first system and drift kJoinDrift times as
// much (straight over kilometers, they read as lines).
constexpr double kJoinHeight = 0.5;
constexpr double kJoinSinuous = 3.0;
constexpr double kJoinDrift = 2.0;
// Around the defects of the phase the waves fade between these coherences (dune_field).
constexpr float kCoherenceLow = 0.01f;
constexpr float kCoherenceHigh = 0.35f;
// The share of the mean of their profiles the waves keep (measured: along 195 lines of 36 km
// across the ergs of waves and 31 across the giants, and over 16 and 13 windows of 10 and 30 km).
constexpr double kWavesKept = 0.9 * 0.84;
constexpr double kGiantsKept = 0.9 * 0.78;

// The trough between two waves sinks this share of their height below the floor.
constexpr double kDig = 0.4;
// The dunes laid on the waves (dune_layers), where the wave is gentler than kLayersSlope
// (tangent, 12 degrees: falling or rising, their sinuous slip faces may face either way) and
// kCrestClear of the wavelength from its crest: oblique ridges in segments (a layer of small
// crests across them drew long straight lines: none).
constexpr double kLayersSlope = 0.22;
constexpr double kCrestClear = 0.03;
constexpr dune_layers::Layer kWavesMedium{700.0, 20.0, 0.12, 0.06, 41};
constexpr dune_layers::Layer kGiantsMedium{1'500.0, 40.0, 0.12, 0.06, 45};
constexpr dune_layers::Layer kGiantsSecond{900.0, 25.0, 0.08, -0.12, 49};
constexpr dune_layers::Layer kGiantsSmall{350.0, 6.0, 0.02, 0.1, 53};
// The ends of the crests: the height falls to kEndsLeast of itself where the noise is low.
constexpr double kEndsLeast = 0.25;
constexpr float kEndsLow = -0.35f;
constexpr float kEndsHigh = 0.15f;
constexpr double kEndsShift = 6'131.0;

// max(0, d), joined smoothly over [-k, k].
double softPositive(double d, double k) {
    if (d <= -k) {
        return 0.0;
    }
    return d >= k ? d : (d + k) * (d + k) / (4.0 * k);
}

double ramp(float e0, float e1, double v) {
    return static_cast<double>(noise::smoothstep(e0, e1, static_cast<float>(v)));
}

double backTanOf(bool giant) {
    return giant ? kGiantsBackTan : kWavesBackTan;
}

// The height of the waves of a nominal spacing, capped by it.
double heightOf(double sand, double amplitude, double nominal, bool giant) {
    const double amp = kAmpBase + kAmpRange * amplitude;
    const double room = nominal / (kLeeCurve / dune_forms::kLeeTan + kBackCurve / backTanOf(giant));
    const double h =
        giant ? std::clamp(kGiantsShare * nominal * amp, kGiantsLow, kGiantsHigh)
              : std::clamp(std::max(kWavesLow + kWavesRange * sand * std::sqrt(sand),
                                    kWavesFill * room) *
                               amp,
                           kWavesLow, kWavesHigh);
    return std::min(h, room);
}

dune_phase::Kind kindOf(bool giant) {
    dune_phase::Kind k;
    k.spacing = giant ? kGiantsSpacing : 1.0;
    k.waves = !giant;
    k.sinuous = giant ? kGiantsSinuous : kWavesSinuous;
    k.drift = giant ? kGiantsDrift : kWavesDrift;
    k.salt = giant ? 31 : 23;
    return k;
}

// How much of the second system rises where its profile is top of its height (joined).
double joinNear(double top) {
    return ramp(static_cast<float>(kJoinTopLow), static_cast<float>(kJoinTopHigh), top);
}

// The dome of the second system: its height where its profile is top of its height full,
// faded by fade (as the first system).
double joinDome(double top, double full, double fade) {
    const double t = std::clamp(top, 0.0, 1.0);
    return kJoinHeight * fade * full * t * t * (3.0 - 2.0 * t);
}

} // namespace

double profile(double t, double wavelength, double height, double backTan) {
    return shapeOf(t, wavelength, height, backTan, 0.0).height;
}

Shape shapeOf(double t, double wavelength, double height, double backTan, double dig) {
    if (!(height > 0.0)) {
        return {};
    }
    const double lee = kLeeCurve * height / dune_forms::kLeeTan;
    // The back: what the face leaves of the wavelength, never shorter than the gentle back needs
    // (where the local wavelength is short, the back and the face overlap: no floor).
    const double back = std::max({wavelength - lee, kBackCurve * height / backTan, 1.0});
    const double d = t * wavelength;
    const double s = d < lee ? 1.0 - d / lee : 0.0;
    const double face = height * s * s;
    const double up = (1.0 - t) * wavelength; // to the next crest, upwind of it
    const double r = up < back ? 1.0 - up / back : 0.0;
    const double rise = height * dmath::pow(r, kBackCurve);
    Shape out;
    out.height = std::max(face, rise);
    // The crest rounded (dune_forms::rounded): the face and the back meet flat on top.
    const double backTop = kBackCurve * height / back;
    if (d < 2.0 * dune_forms::kCrestRound / dune_forms::kLeeTan) {
        out.height = std::min(out.height, height - dune_forms::rounded(d, dune_forms::kLeeTan));
    }
    if (up < 2.0 * dune_forms::kCrestRound / backTop) {
        out.height = std::min(out.height, height - dune_forms::rounded(up, backTop));
    }
    // The slopes the layers read, falling and rising downwind: the face, and the back growing
    // linearly from its foot to its steepest at the crest (its own slope grows as r^0.4, too
    // fast for the layers that read it).
    out.slope = dune_forms::kLeeTan * s;
    out.rise = kBackCurve * height / back * r;
    // The trough between two waves: from the middle of the face to the middle of the back,
    // ending at the next crest at the latest (where the phase starts over); its near half falls
    // with the face, its far half rises with the back.
    const double from = 0.5 * lee;
    const double to = std::min(lee + 0.5 * back, wavelength);
    const double depth = dig * height;
    out.height -= dune_forms::trough(d, from, to, depth);
    const double deeper = dune_forms::trough(d + 0.5, from, to, depth) -
                          dune_forms::trough(d - 0.5, from, to, depth);
    out.slope += std::max(0.0, deeper);
    out.rise += std::max(0.0, -deeper);
    out.crest = std::min(d, up);
    return out;
}

double joined(double main, double join, double top, double share, double soft) {
    const double near = joinNear(top);
    if (!(near > 0.0) || !(share > 0.0)) {
        return main;
    }
    return main + share * near * softPositive(join - main, std::max(soft, 1.0));
}

double waves(const Ergs::Impl& m, const dune_phase::Shared& s, double x, double z, double sand,
             double amplitude, bool giant) {
    const dune_phase::Pivots p = dune_phase::pivotsAt(m, s, giant);
    const dune_phase::Kind first = kindOf(giant);
    // Where the crests fork (the second system) and where they end (saddles): noises of a few
    // wavelengths, the ends noise for the big waves, the drift for the giant ones.
    const noise::FbmOctaves& slow = giant ? m.drift : m.ends;
    const double joins = ramp(kJoinLow, kJoinHigh,
                              static_cast<double>(slow.fbm2D(x - kJoinShift, z + kJoinShift,
                                                             memosOf(m).waveJoins)));
    const double ends = kEndsLeast + (1.0 - kEndsLeast) *
                        ramp(kEndsLow, kEndsHigh,
                             static_cast<double>(slow.fbm2D(x + kEndsShift, z - kEndsShift,
                                                            memosOf(m).waveEnds)));
    const dune_phase::Pivots fine = giant ? dune_phase::pivotsAt(m, s, false) : p;
    // The height of the first wave system, with the dunes laid on it.
    const auto waveOf = [&](const dune_phase::Phase& ph) {
        // Faded at the ends of the crests and the defects of the phase: the whole form scaled
        // down, so that its fading never adds to the slope of its face.
        const double fade = ends * ramp(kCoherenceLow, kCoherenceHigh, ph.coherence);
        const double full = heightOf(sand, amplitude, ph.nominal, giant);
        const double h = fade * full;
        // Where the phase stretches (its local wavelength over the nominal one, near its
        // defects), the wavelength varies fast: the distance to the crest, fraction times it,
        // would grow too fast and steepen the faces; the nominal one then (smooth, gentler).
        const double wavelength = std::min(ph.wavelength, ph.nominal);
        Shape shape = shapeOf(ph.fraction, wavelength, full, backTanOf(giant), kDig);
        shape.height *= fade;
        shape.slope *= fade;
        shape.rise *= fade;
        // The dunes on the waves: where the wave is gentler than kLayersSlope, clear of its crest.
        const double on = std::clamp(1.0 - (shape.slope + shape.rise) / kLayersSlope, 0.0, 1.0) *
                          ramp(0.0f, static_cast<float>(kCrestClear * wavelength), shape.crest);
        const dune_layers::Layer medium = giant ? kGiantsMedium : kWavesMedium;
        const double second =
            giant ? std::max(dune_layers::layer(m, s, fine, kGiantsSecond, h, on),
                             dune_layers::layer(m, s, fine, kGiantsSmall, h, on))
                  : 0.0;
        return shape.height + std::max(dune_layers::layer(m, s, fine, medium, h, on), second);
    };
    if (!(joins > 0.0)) {
        return waveOf(dune_phase::phaseAt(m, s, p, first));
    }
    dune_phase::Kind second = first;
    second.spacing *= kJoinSpacing;
    second.turn = kJoinTurn;
    second.salt += 1;
    second.sinuous *= kJoinSinuous;
    second.drift *= kJoinDrift;
    const std::array<dune_phase::Phase, 2> both = dune_phase::phasesAt(m, s, p, first, second);
    // The joins rise over the first system by their share near their crests (both dig troughs:
    // a share of a trough would lift it in a step where the share begins).
    const double main = waveOf(both[0]);
    const dune_phase::Phase& ph = both[1];
    const double full = heightOf(sand, amplitude, ph.nominal, giant);
    const double top =
        full > 0.0 ? shapeOf(ph.fraction, std::min(ph.wavelength, ph.nominal), full,
                             backTanOf(giant), kDig).height / full
                   : 0.0;
    const double fade = ends * ramp(kCoherenceLow, kCoherenceHigh, ph.coherence);
    return joined(main, joinDome(top, full, fade), top, joins, kJoinSoft * full);
}

double meanWaves(const Ergs::Impl& m, const dune_phase::Shared& s, double sand, double amplitude,
                 bool giant) {
    const dune_phase::Pivots p = dune_phase::pivotsAt(m, s, giant);
    const double spacing =
        giant ? kGiantsSpacing * dune_phase::nominalAt(m, s, p) : dune_phase::nominalAt(m, s, p, true);
    const double h = heightOf(sand, amplitude, spacing, giant);
    const double lee = kLeeCurve * h / dune_forms::kLeeTan;
    const double back = std::max(spacing - lee, kBackCurve * h / backTanOf(giant));
    // The face holds h lee / 3, the back h back / (1 + kBackCurve); the ends, the defects of the
    // phase, the troughs and the layers keep the measured share of it.
    return (giant ? kGiantsKept : kWavesKept) * h * (lee / 3.0 + back / (1.0 + kBackCurve)) /
           spacing;
}

double spacingScale(bool giant) {
    // The giants: 6 to 19 km against about 2 km for the realistic ridges.
    return giant ? kGiantsFilterScale : kWavesFilterScale;
}

} // namespace terrain::dune_waves
