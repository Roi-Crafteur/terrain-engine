// The dunes laid on the dunes (dune_layers.hpp).
#include "dune_layers.hpp"

#include "dune_forms.hpp"
#include "ergs_impl.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace terrain::dune_layers {

namespace {

// Their rows follow that of the pivots (1 to 3 km) over kPivotSpacing: they vary from place to
// place; their cells across are kCellShare of the rows; the rows wander kSinuous times as much
// as the ridges.
constexpr double kPivotSpacing = 2'000.0;
constexpr double kCellShare = 1.1;
constexpr double kSinuous = 1.5;
constexpr float kCoherenceLow = 0.03f; // they fade around the defects of their phases
constexpr float kCoherenceHigh = 0.3f;
// They gather in groups and leave voids (the clusters noise of the ergs, 9 km): none below
// kVoidLow, all of the layer above kVoidHigh, faded smoothly over the ground between (a crescent
// across the edge of a void is lower on one side, never cut). Each crescent reads at its middle
// (estimated from the phases and the wind: the same over it) the noise of the barchans (1.2 km)
// sampled kShift times the salt away: there or not (within kPresenceSoft of its occupancy, from
// kOccupancy at the edges of a group to kOccupancy + kGroupOccupancy in its heart), its size
// (kSmallest to 1, most of them small: its height and its width together, from kNarrowest to
// kNarrowest + kWidthRange: the widest nearly join their neighbors in curved ridges) and where
// its crest lies along its row.
constexpr float kVoidLow = 0.05f;
constexpr float kVoidHigh = 0.3f;
constexpr double kShift = 1'913.0;
constexpr double kOccupancy = 0.35;
constexpr double kGroupOccupancy = 0.45;
constexpr double kPresenceSoft = 0.3;
constexpr double kSmallest = 0.15;
constexpr double kSizePower = 2.5;
constexpr double kNarrowest = 0.6;
constexpr double kWidthRange = 1.25;

double ramp(double e0, double e1, double v) {
    const double t = std::clamp((v - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

double layer(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
             const Layer& l, double height, double share) {
    if (!(share > 0.0)) {
        return 0.0;
    }
    ErgMemos& memos = memosOf(m);
    const double group = ramp(kVoidLow, kVoidHigh,
                              static_cast<double>(m.clusters.fbm2D(s.x[0], s.z[0], memos.clusters)));
    share *= group;
    if (!(share > 0.0)) {
        return 0.0; // a void
    }
    dune_phase::Kind rowsKind;
    rowsKind.turn = l.turn;
    rowsKind.spacing = l.spacing / kPivotSpacing;
    rowsKind.sinuous = kSinuous;
    rowsKind.salt = l.salt;
    dune_phase::Kind cellsKind = rowsKind;
    cellsKind.across = true;
    cellsKind.spacing = kCellShare * rowsKind.spacing;
    cellsKind.salt = l.salt + 1;
    const std::array<dune_phase::Phase, 2> both = dune_phase::phasesAt(m, s, p, rowsKind, cellsKind);
    const dune_phase::Phase& rows = both[0];
    const dune_phase::Phase& cells = both[1];
    const double coherent = share * ramp(kCoherenceLow, kCoherenceHigh, rows.coherence) *
                            ramp(kCoherenceLow, kCoherenceHigh, cells.coherence);
    if (!(coherent > 0.0)) {
        return 0.0;
    }
    const double tall = l.base + l.share * std::max(0.0, height);
    const std::array<double, 2> wind = windOf(m, s.x[0], s.z[0]);
    const dmath::SineCosine turn = dmath::sinCosTurns(l.turn);
    const double wx = wind[0] * turn.cosine - wind[1] * turn.sine;
    const double wz = wind[0] * turn.sine + wind[1] * turn.cosine;
    const double shift = kShift * static_cast<double>(l.salt);
    double best = 0.0;
    const double occupancy = kOccupancy + kGroupOccupancy * group;
    const auto memo = static_cast<std::size_t>(((l.salt / 4) % 4) * 6);
    for (int back = -1; back <= 1; ++back) {
        const double along = (rows.fraction + back - 0.6) * rows.wavelength;
        const double lateral = (cells.fraction - 0.5) * cells.wavelength;
        const double cx = s.x[0] - along * wx + lateral * wz + shift;
        const double cz = s.z[0] - along * wz - lateral * wx - shift;
        const auto at = memo + static_cast<std::size_t>(2 * (back + 1));
        const double n = static_cast<double>(m.scatter.fbm2D(cx, cz, memos.crescents[at]));
        const double o =
            static_cast<double>(m.scatter.fbm2D(cx + shift, cz + shift, memos.crescents[at + 1]));
        dune_forms::Barchan b;
        b.t = rows.fraction + back;
        b.wavelength = rows.wavelength;
        b.across = cells.fraction;
        b.cell = cells.wavelength;
        b.single = true;
        b.height = tall;
        const double size =
            kSmallest + (1.0 - kSmallest) * dmath::pow(std::clamp(0.5 + o, 0.0, 1.0), kSizePower);
        b.presence = coherent * ramp(-kPresenceSoft, kPresenceSoft, occupancy - (0.5 + 1.6 * n)) * size;
        b.width = kNarrowest + kWidthRange * size;
        b.crest = 0.45 + 0.3 * std::clamp(0.5 + n, 0.0, 1.0);
        best = std::max(best, dune_forms::barchan(b));
    }
    return best;
}

} // namespace terrain::dune_layers
