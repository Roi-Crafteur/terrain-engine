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
constexpr double kGiantsBackTan = 0.24932800284318068; // tan 14 degrees
// Spacing over that of the pivots (1 to 3 km by the sand).
constexpr double kWavesSpacing = 1.5;
constexpr double kGiantsSpacing = 1.0; // their pivots hold their spacing (dune_phase)
constexpr double kGiantsFilterScale = 6.5;
// The noises of the sinuosity (5 km) and of the drift (12 km) keep their wavelengths: the wider
// the waves, the less of them they take, or their crests would scatter.
constexpr double kWavesSinuous = 1.0;
constexpr double kWavesDrift = 0.5;
constexpr double kGiantsSinuous = 0.35;
constexpr double kGiantsDrift = 0.15;
// Heights: big waves kWavesLow + kWavesRange sand^1.5 (within kWavesLow and kWavesHigh); giant
// waves kGiantsShare of their spacing (within kGiantsLow and kGiantsHigh); both times the noise
// of the heights.
constexpr double kWavesLow = 80.0;
constexpr double kWavesRange = 220.0;
constexpr double kWavesHigh = 300.0;
constexpr double kGiantsShare = 0.07;
constexpr double kGiantsLow = 300.0;
constexpr double kGiantsHigh = 800.0;
constexpr double kAmpBase = 0.92;
constexpr double kAmpRange = 0.25;
// The second system: its spacing over the first, its turn, where it lies (the noise of the edges
// of the ergs, 70 km, sampled kJoinShift away: a faster one would raise the second system in
// patches whose edges, at these heights, would be cliffs) and how much.
constexpr double kJoinSpacing = 1.6;
constexpr double kJoinTurn = 0.07; // turns: 25 degrees
constexpr double kJoinShift = 919'191.0;
constexpr float kJoinLow = 0.05f;
constexpr float kJoinHigh = 0.3f;
// Around the defects of the phase the waves fade between these coherences (dune_field).
constexpr float kCoherenceLow = 0.01f;
constexpr float kCoherenceHigh = 0.12f;
constexpr double kCoherentMean = 0.9;

// The trough between two waves sinks this share of their height below the floor.
constexpr double kDig = 0.4;
// The dunes laid on the waves (dune_layers), where the wave is gentler than kLayersSlope
// (tangent, 20 degrees) and kCrestClear of the wavelength from its crest: oblique ridges in
// segments (a layer of small crests across them drew long straight lines: none).
constexpr double kLayersSlope = 0.36;
constexpr double kCrestClear = 0.03;
constexpr dune_layers::Layer kWavesMedium{700.0, 20.0, 0.12, 0.06, 41};
constexpr dune_layers::Layer kGiantsMedium{1'500.0, 40.0, 0.12, 0.06, 45};

double ramp(float e0, float e1, double v) {
    return static_cast<double>(noise::smoothstep(e0, e1, static_cast<float>(v)));
}

double backTanOf(bool giant) {
    return giant ? kGiantsBackTan : kWavesBackTan;
}

// The height of the waves of a nominal spacing, capped by it.
double heightOf(double sand, double amplitude, double nominal, bool giant) {
    const double amp = kAmpBase + kAmpRange * amplitude;
    const double h = giant ? std::clamp(kGiantsShare * nominal * amp, kGiantsLow, kGiantsHigh)
                           : std::clamp((kWavesLow + kWavesRange * sand * std::sqrt(sand)) * amp,
                                        kWavesLow, kWavesHigh);
    const double room = nominal / (kLeeCurve / dune_forms::kLeeTan + kBackCurve / backTanOf(giant));
    return std::min(h, room);
}

dune_phase::Kind kindOf(bool giant) {
    dune_phase::Kind k;
    k.spacing = giant ? kGiantsSpacing : kWavesSpacing;
    k.sinuous = giant ? kGiantsSinuous : kWavesSinuous;
    k.drift = giant ? kGiantsDrift : kWavesDrift;
    k.salt = giant ? 31 : 23;
    return k;
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
    if (face >= rise) {
        out.height = face;
        out.slope = dune_forms::kLeeTan * s;
    } else {
        out.height = rise;
        out.slope = kBackCurve * height / back * dmath::pow(r, kBackCurve - 1.0);
    }
    // The trough between two waves: from the middle of the face to the middle of the back.
    out.height -= dune_forms::trough(d, 0.5 * lee, lee + 0.5 * back, dig * height);
    out.crest = std::min(d, up);
    return out;
}

double waves(const Ergs::Impl& m, const dune_phase::Shared& s, double x, double z, double sand,
             double amplitude, bool giant) {
    const dune_phase::Pivots p = dune_phase::pivotsAt(m, s, giant);
    const dune_phase::Kind first = kindOf(giant);
    const dune_phase::Pivots fine = giant ? dune_phase::pivotsAt(m, s, false) : p;
    const auto waveOf = [&](const dune_phase::Phase& ph) {
        const double h = heightOf(sand, amplitude, ph.nominal, giant) *
                         ramp(kCoherenceLow, kCoherenceHigh, ph.coherence);
        const Shape shape = shapeOf(ph.fraction, ph.wavelength, h, backTanOf(giant), kDig);
        // The dunes on the waves: where the wave is gentler than kLayersSlope, clear of its crest.
        const double on = std::clamp(1.0 - shape.slope / kLayersSlope, 0.0, 1.0) *
                          ramp(0.0f, static_cast<float>(kCrestClear * ph.wavelength), shape.crest);
        const dune_layers::Layer medium = giant ? kGiantsMedium : kWavesMedium;
        return shape.height + dune_layers::layer(m, s, fine, medium, h, on);
    };
    const double joins = ramp(kJoinLow, kJoinHigh,
                              static_cast<double>(m.edges.fbm2D(z - kJoinShift, x + kJoinShift,
                                                                memosOf(m).waveJoins)));
    if (!(joins > 0.0)) {
        return waveOf(dune_phase::phaseAt(m, s, p, first));
    }
    dune_phase::Kind second = first;
    second.spacing *= kJoinSpacing;
    second.turn = kJoinTurn;
    second.salt += 1;
    const std::array<dune_phase::Phase, 2> both = dune_phase::phasesAt(m, s, p, first, second);
    return std::max(waveOf(both[0]), joins * waveOf(both[1]));
}

double meanWaves(const Ergs::Impl& m, const dune_phase::Shared& s, double sand, double amplitude,
                 bool giant) {
    const dune_phase::Pivots p = dune_phase::pivotsAt(m, s, giant);
    const double spacing = (giant ? kGiantsSpacing : kWavesSpacing) *
                           dune_phase::nominalAt(m, s, p);
    const double h = heightOf(sand, amplitude, spacing, giant);
    const double lee = kLeeCurve * h / dune_forms::kLeeTan;
    const double back = std::max(spacing - lee, kBackCurve * h / backTanOf(giant));
    // The face holds h lee / 3, the back h back / (1 + kBackCurve).
    return kCoherentMean * h * (lee / 3.0 + back / (1.0 + kBackCurve)) / spacing;
}

double spacingScale(bool giant) {
    // The giants: 6 to 19 km against about 2 km for the realistic ridges.
    return giant ? kGiantsFilterScale : kWavesSpacing;
}

} // namespace terrain::dune_waves
