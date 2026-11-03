// The waves of the ergs of the characters big waves and giant waves (dune_waves.hpp).
#include "dune_waves.hpp"

#include "dune_chutes.hpp"
#include "dune_forms.hpp"
#include "dune_layers.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace terrain::dune_waves {

namespace {

// The giant waves: a straight slip face down kGiantsSlip of their height (an avalanche face
// readable from kilometers; the parabola alone made the wall of 700 m a low smooth hill).
constexpr double kGiantsSlip = 0.35;
// Their faces fall at 31 degrees (slip faces stand at 30 to 34; near the defects of their phase,
// less: kDefectLean).
constexpr double kGiantsLeeTan = 0.6008606190275604;
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
constexpr double kJoinTopLow = 0.1;
constexpr double kJoinTopHigh = 1.0;
constexpr double kJoinSoft = 0.15;
// The joins below the crests of the first system: kJoinHeight of the height of their own, a dome
// over their profile (a smoothstep of it, flat on its crest: a sharp crest barely over the first
// system drew a fine straight line; never steeper than their face), with no dunes laid on them.
// Their crests wander kJoinSinuous times as much as those of the first system and drift kJoinDrift
// times as much (straight over kilometers, they read as lines).
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
constexpr double kGiantsBackShare = 0.25;
constexpr double kGiantsCrestClear = 4.0;
constexpr double kCoverLow = 0.05;
constexpr double kCoverHigh = 0.3;
constexpr dune_layers::Layer kWavesMedium{700.0, 20.0, 0.12, 0.06, 41};
// Their medium crescents 1,700 m apart (1,500 until their crescents read where they lie at their
// middles: whole, never cut, the smallest fewer, and their sizes varied less than 5 times).
constexpr dune_layers::Layer kGiantsMedium{1'700.0, 40.0, 0.12, 0.06, 45};
constexpr dune_layers::Layer kGiantsSecond{900.0, 25.0, 0.08, -0.12, 49};
// Small ones 250 m apart on their flanks and crests, by their bed alone (350 m apart, under the
// others where it wanted them: sizes varied 4.7 times only on the erg of the site, c1119e3).
constexpr dune_layers::Layer kGiantsSmall{250.0, 8.0, 0.03, 0.1, 53};
// The ends of the crests: the height falls to kEndsLeast of itself where the noise is low.
constexpr double kEndsLeast = 0.25;
constexpr float kEndsLow = -0.35f;
constexpr float kEndsHigh = 0.15f;
constexpr double kEndsShift = 6'131.0;

// max(0, d), joined smoothly over [-k, k]: its slope and its curvature continuous (a jump of the
// curvature where the joins meet the first system drew a fine line under the light).
double softPositive(double d, double k) {
    if (d <= -k) {
        return 0.0;
    }
    return d >= k ? d : (d + k) * (d + k) * (d + k) * (3.0 * k - d) / (16.0 * k * k * k);
}

// smoothstep with a continuous curvature, [0, 1].
double smoother(double e0, double e1, double v) {
    const double t = std::clamp((v - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * t * (t * (6.0 * t - 15.0) + 10.0);
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
    return smoother(kJoinTopLow, kJoinTopHigh, top);
}

// The dome of the second system: its height where its profile is top of its height full,
// faded by fade (as the first system).
double joinDome(double top, double full, double fade) {
    const double t = std::clamp(top, 0.0, 1.0);
    return kJoinHeight * fade * full * t * t * (3.0 - 2.0 * t);
}

// Where the crests of the waves end (saddles): the height falls to kEndsLeast of itself where
// the slow noise is low.
double endsOf(const noise::FbmOctaves& slow, const Ergs::Impl& m, double x, double z) {
    return kEndsLeast +
           (1.0 - kEndsLeast) * ramp(kEndsLow, kEndsHigh,
                                     static_cast<double>(slow.fbm2D(x + kEndsShift, z - kEndsShift,
                                                                    memosOf(m).waveEnds)));
}

// The ground a layer of dunes lies on: a wave of height full at fraction of its wavelength, the
// gradient of its phase (gx, gz: cycles per meter), the share of their slope its backs keep them
// up to, how far from its crests they keep clear and where its slip face ends (face: meters
// downwind of its crest, where it is no steeper than kLayersSlope).
struct Bed {
    double fraction = 0.0;
    double wavelength = 1.0;
    double full = 0.0;
    double gx = 0.0;
    double gz = 0.0;
    double backShare = 1.0;
    double clear = 0.0;
    double face = 0.0;
    double leeTan = kGiantsLeeTan; // of the straight slip face of the giant waves
    double defect = 1.0;           // how much of the crescents the defects of the phase leave
    bool giant = false;
};

// Near the defects of the phase of the giant waves (its coherence from kLeanLow up to kLeanHigh),
// their straight slip faces lean back to kDefectLean of their slope, the curve of their backs
// bends by it to the power kBackLean (gentler at their crests): there the distance to their crest grows up to 1.3 times faster
// than the ground (the local wavelength changes by 4 m a meter) and their fade tilts them (a face
// of 31 degrees stood at 38, a back of 16 at 21). The coherence varies over kilometers: the lean
// adds a few thousandths of slope. Their crescents leave the ground the fade of the ends of their
// crests tilts from kTiltLow to kTiltHigh (up to 0.26 over 700 m; a crescent stood at 39 on it).
constexpr double kDefectLean = 0.85;
constexpr double kBackLean = 2.0;
constexpr double kLeanLow = 0.3;
constexpr double kLeanHigh = 0.5;
constexpr double kTiltLow = 0.06;
constexpr double kTiltHigh = 0.15;

// The crescents of the giant waves keep kRoomMargin meters from their crests and from the feet of
// their slip faces (the festoons shift them by up to 42 m along their crests, which bend: the
// middles of the crescents read their profile from its phase and gradient at the point).
constexpr double kRoomMargin = 150.0;

// Where the slip face of a giant wave of height full ends: its parabola as steep as kLayersSlope.
double faceEnd(double full, double leeTan) {
    const double straight = kGiantsSlip * full;
    return straight / leeTan +
           kLeeCurve * (full - straight) / leeTan * (1.0 - kLayersSlope / leeTan);
}

Bed bedOf(double fraction, double wavelength, double full, bool giant) {
    Bed bed;
    bed.fraction = fraction;
    bed.wavelength = wavelength;
    bed.full = full;
    bed.giant = giant;
    // The giant waves keep their dunes on their backs up to kGiantsBackShare of their slope (at
    // most 16 degrees: their slip faces, downwind, face up the back) and nearer their crests:
    // clear of them over kGiantsCrestClear times the height of their dunes at least (rising over
    // less, they would stand at more than 40 degrees behind the crest), the big waves over
    // kCrestClear of the wavelength.
    bed.backShare = giant ? kGiantsBackShare : 1.0;
    bed.clear = giant ? kGiantsCrestClear * (kGiantsMedium.base + kGiantsMedium.share * full)
                      : kCrestClear * wavelength;
    return bed;
}

Shape bedShape(const Bed& b, double fraction) {
    return shapeOf(fraction, b.wavelength, b.full, backTanOf(b.giant), kDig,
                   b.giant ? kGiantsSlip : 0.0, b.giant ? b.leeTan : dune_forms::kLeeTan,
                   b.giant ? dmath::pow(b.leeTan / kGiantsLeeTan, kBackLean) : 1.0);
}

// The dunes on the waves: where the wave (its slopes faded by fade) is gentler than kLayersSlope,
// clear of its crest (on), and how much the giant waves want them (cover: their windward backs and
// their crests, few in the corridors: their profile over their height, kCoverLow to kCoverHigh).
void lieOn(const Bed& b, const Shape& shape, double fade, double& on, double& cover) {
    cover = b.giant && b.full > 0.0 ? smoother(kCoverLow, kCoverHigh, shape.height / b.full) : -1.0;
    on = std::clamp(1.0 - (shape.slope * fade + b.backShare * (shape.rise * fade)) / kLayersSlope,
                    0.0, 1.0) *
         ramp(0.0f, static_cast<float>(b.clear), shape.crest);
}

// The bed as a crescent reads it at its middle, dx, dz meters away (dune_layers::Host): the same
// wave, its phase dx, dz farther, unfaded (its fade there unknown: read at the point, it changed
// within the crescent); its room downwind to its next crest, upwind to the foot of its last slip
// face, kRoomMargin less.
void bedAt(const void* context, double dx, double dz, double& share, double& cover,
           double& ahead, double& behind) {
    const Bed& b = *static_cast<const Bed*>(context);
    double fraction = b.fraction + dx * b.gx + dz * b.gz;
    fraction -= std::floor(fraction);
    lieOn(b, bedShape(b, fraction), 1.0, share, cover);
    share *= b.defect;
    ahead = std::max(0.0, (1.0 - fraction) * b.wavelength - kRoomMargin);
    behind = std::max(0.0, fraction * b.wavelength - b.face - kRoomMargin);
}

// The crescents of the giant waves, each layer faded out on the far horizon where the side of its
// samples (Shared::footprint) passes from a fifth to a third of its spacing (smaller, it would
// draw them in rows of spots, as the chutes: kFarFrom to kFarTo of the spacing).
constexpr double kFarFrom = 0.18;
constexpr double kFarTo = 0.36;

double farLayer(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& fine,
                const dune_layers::Layer& l, double full, const dune_layers::Host& host) {
    const double near = 1.0 - smoother(kFarFrom * l.spacing, kFarTo * l.spacing, s.footprint);
    return near > 0.0 ? near * dune_layers::layer(m, s, fine, l, full, host) : 0.0;
}

double giantLayers(const Ergs::Impl& m, const dune_phase::Shared& s,
                   const dune_phase::Pivots& fine, double full, const dune_layers::Host& host,
                   const dune_layers::Host& small) {
    const double second = std::max(farLayer(m, s, fine, kGiantsSecond, full, host),
                                   farLayer(m, s, fine, kGiantsSmall, full, small));
    return std::max(farLayer(m, s, fine, kGiantsMedium, full, host), second);
}

// The dunes laid on a wave (bed, of phase ph), faded to the height h, where they lie at the point
// by on and cover (the big waves). Each crescent of the giant waves reads where it lies at its
// middle (dune_layers::Host), its height by that of the wave unfaded (faded, it changed within
// it); none reaches their slip faces (nothing to compute there).
double layersOn(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& fine,
                Bed& bed, const dune_phase::Phase& ph, double h, double on, double cover) {
    if (!bed.giant) {
        return dune_layers::layer(m, s, fine, kWavesMedium, h, on, cover);
    }
    if (!m.params.giantCrescents) {
        return 0.0;
    }
    bed.face = faceEnd(bed.full, bed.leeTan);
    if (bed.fraction * bed.wavelength < bed.face) {
        return 0.0;
    }
    bed.gx = ph.gx;
    bed.gz = ph.gz;
    const dune_layers::Host host{&bed, bedAt, false};
    const dune_layers::Host small{&bed, bedAt, false}; // where they lie, not by the cover
    return giantLayers(m, s, fine, bed.full, host, small);
}

// How much of the crescents the fade of a giant wave leaves on its back at (x, z), unfaded height
// meters high: the tilt of their bed, that height times the change of the fade per meter by its
// ends (their noise read 1 m east and south) and its coherence (its gradient).
double crescentsLeft(const Ergs::Impl& m, const noise::FbmOctaves& slow, const dune_phase::Phase& ph,
                     double x, double z, double ends, double height) {
    const double coherent = ramp(kCoherenceLow, kCoherenceHigh, ph.coherence);
    double fx = (endsOf(slow, m, x + 1.0, z) - ends) * coherent;
    double fz = (endsOf(slow, m, x, z + 1.0) - ends) * coherent;
    if (ph.coherence < static_cast<double>(kCoherenceHigh)) {
        // The ramp of the coherence (smoothstep) times its gradient (dune_phase).
        const double width = static_cast<double>(kCoherenceHigh - kCoherenceLow);
        const double t = (ph.coherence - static_cast<double>(kCoherenceLow)) / width;
        const double slope = t > 0.0 ? 6.0 * t * (1.0 - t) / width : 0.0;
        fx += ends * slope * ph.cx;
        fz += ends * slope * ph.cz;
    }
    const double tilt = std::max(0.0, height) * dmath::hypot(fx, fz);
    return 1.0 - smoother(kTiltLow, kTiltHigh, tilt);
}

} // namespace

double profile(double t, double wavelength, double height, double backTan) {
    return shapeOf(t, wavelength, height, backTan, 0.0).height;
}

Shape shapeOf(double t, double wavelength, double height, double backTan, double dig,
              double slip, double leeTan, double backBend) {
    if (!(height > 0.0)) {
        return {};
    }
    // The face: a straight slip face at the angle of repose down slip of the height, then the
    // parabola to the trough, its slope that of the slip face where they meet.
    const double straight = std::clamp(slip, 0.0, 1.0) * height;
    const double slipRun = straight / leeTan;
    const double curveRun = kLeeCurve * (height - straight) / leeTan;
    const double lee = slipRun + curveRun;
    // The back and the trough keep the length of the face without its straight slip face (shorter,
    // the floor between two waves would grow flat).
    const double leeRef = kLeeCurve * height / leeTan;
    // The back: what the face leaves of the wavelength, never shorter than the gentle back needs
    // (where the local wavelength is short, the back and the face overlap: no floor).
    const double back =
        std::max({wavelength - leeRef, kBackCurve * height / backTan, 1.0});
    const double d = t * wavelength;
    const double s = d < slipRun ? 1.0 : (d < lee ? 1.0 - (d - slipRun) / curveRun : 0.0);
    const double face =
        d < slipRun ? height - leeTan * d : (height - straight) * s * s;
    const double up = (1.0 - t) * wavelength; // to the next crest, upwind of it
    const double r = up < back ? 1.0 - up / back : 0.0;
    const double curve = kBackCurve * backBend;
    const double rise = height * dmath::pow(r, curve);
    Shape out;
    out.height = std::max(face, rise);
    // The crest rounded (dune_forms::rounded): the face and the back meet flat on top.
    const double backTop = curve * height / back;
    if (d < 2.0 * dune_forms::kCrestRound / leeTan) {
        out.height = std::min(out.height, height - dune_forms::rounded(d, leeTan));
    }
    if (up < 2.0 * dune_forms::kCrestRound / backTop) {
        out.height = std::min(out.height, height - dune_forms::rounded(up, backTop));
    }
    // The slopes the layers read, falling and rising downwind: the face, and the back growing
    // linearly from its foot to its steepest at the crest (its own slope grows as r^0.4, too
    // fast for the layers that read it).
    out.slope = leeTan * s;
    out.rise = curve * height / back * r;
    // The trough between two waves: from the middle of the face to the middle of the back,
    // ending at the next crest at the latest (where the phase starts over); its near half falls
    // with the face, its far half rises with the back.
    const double from = 0.5 * lee;
    const double to = std::min(leeRef + 0.5 * back, wavelength);
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
    const double ends = endsOf(slow, m, x, z);
    const dune_phase::Pivots fine = giant ? dune_phase::pivotsAt(m, s, false) : p;
    const double festoon =
        giant ? dune_chutes::festoons(m, s, x, z) : 0.0;
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
        // Never longer than the local one unclamped either (the phase compressed past
        // kShortest of its nominal spacing: the faces 1.2 times steeper, at 40 degrees).
        const double wavelength = std::min({ph.wavelength, ph.nominal, ph.local});
        // The giant waves: their crests and slip faces in festoons and spurs (the whole profile
        // shifted downwind by the festoons there: continuous where the phase starts over).
        double fraction = ph.fraction;
        if (giant) {
            fraction -= festoon / wavelength;
            fraction -= std::floor(fraction);
        }
        Bed bed = bedOf(fraction, wavelength, full, giant);
        if (giant) {
            const double coherent = smoother(kLeanLow, kLeanHigh, ph.coherence);
            bed.leeTan = kGiantsLeeTan * (kDefectLean + (1.0 - kDefectLean) * coherent);
        }
        Shape shape = bedShape(bed, fraction);
        double on = 0.0;
        double cover = -1.0;
        lieOn(bed, shape, fade, on, cover);
        bed.defect = giant && fraction * wavelength >= faceEnd(full, bed.leeTan)
                         ? crescentsLeft(m, slow, ph, x, z, ends, shape.height) : 1.0;
        shape.height *= fade;
        if (giant) {
            shape.height -= fade * dune_chutes::depth(m, s, x, z, fraction * wavelength,
                                                      kGiantsSlip * full / bed.leeTan);
        }
        return shape.height + layersOn(m, s, fine, bed, ph, h, on, cover);
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
