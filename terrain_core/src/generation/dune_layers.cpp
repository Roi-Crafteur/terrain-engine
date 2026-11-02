// The dunes laid on the dunes (dune_layers.hpp).
#include "dune_layers.hpp"

#include "dune_forms.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>

namespace terrain::dune_layers {

namespace {

constexpr double kGap = 0.15;                 // the floor between two of them, share
// Their spacing follows that of the pivots (1 to 3 km) over kPivotSpacing: it varies from place
// to place; their crests wander kSinuous times as much as the ridges, and break into segments
// where the noise of the barchans (1.2 km), sampled kBreakShift times their salt away, is low.
constexpr double kPivotSpacing = 2'000.0;
constexpr double kSinuous = 4.0;
constexpr double kBreakShift = 1'913.0;
constexpr float kBreakLow = -0.25f;
constexpr float kBreakHigh = 0.1f;
constexpr float kCoherenceLow = 0.03f;         // they fade around the defects of their phase
constexpr float kCoherenceHigh = 0.3f;

} // namespace

double layer(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
             const Layer& l, double height, double share) {
    if (!(share > 0.0)) {
        return 0.0;
    }
    dune_phase::Kind k;
    k.turn = l.turn;
    k.spacing = l.spacing / kPivotSpacing;
    k.sinuous = kSinuous;
    k.salt = l.salt;
    const dune_phase::Phase phase = dune_phase::phaseAt(m, s, p, k);
    const double shift = kBreakShift * static_cast<double>(l.salt);
    const float pieces = m.scatter.fbm2D(s.x[0] + shift, s.z[0] - shift);
    const double on = share *
                      static_cast<double>(noise::smoothstep(kCoherenceLow, kCoherenceHigh,
                                                            static_cast<float>(phase.coherence))) *
                      static_cast<double>(noise::smoothstep(kBreakLow, kBreakHigh, pieces));
    const double tall = l.base + l.share * std::max(0.0, height);
    return on * dune_forms::transverse(phase.fraction, phase.wavelength, tall, kGap, phase.nominal);
}

} // namespace terrain::dune_layers
