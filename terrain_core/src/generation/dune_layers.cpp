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
// (kSmallest to 1, most of them small: its width up to kWidest, the widest nearly joining their
// neighbors in curved ridges, and its height with it) and where
// its crest lies along its row.
constexpr double kCorridorShare = 0.3;
constexpr double kCoverOccupancy = 0.3; // more of them where the form below wants them
constexpr double kCoverSizePower = 0.7;
constexpr float kVoidLow = 0.05f;
constexpr float kVoidHigh = 0.3f;
constexpr double kShift = 1'913.0;
constexpr double kOccupancy = 0.35;
constexpr double kGroupOccupancy = 0.45;
constexpr double kPresenceSoft = 0.3;
constexpr double kSmallest = 0.15;
// Where its crest lies along its row: kCrestLow to kCrestLow + kCrestRange of it.
constexpr double kCrestLow = 0.45;
constexpr double kCrestRange = 0.3;
constexpr double kSizePower = 2.5;
constexpr double kWidest = 1.85;

double ramp(double e0, double e1, double v) {
    const double t = std::clamp((v - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Where the crescents lie (share) and how much the form below wants them (cover), with the
// groups and the voids (group: that of the clusters noise, raised to the cover).
struct Lie {
    double share = 0.0;
    double group = 0.0;
    double cover = -1.0;
};

Lie lieOf(double share, double cover, double groups) {
    Lie out;
    out.group = groups;
    out.cover = cover;
    if (cover >= 0.0) {
        out.group = std::max(out.group, cover);
        share *= kCorridorShare + (1.0 - kCorridorShare) * cover;
    }
    out.share = share * out.group;
    return out;
}

// The layer, its crescents lying by point (the same at the point for all of them), else by the
// host at their middles.
// A layer at the point: its rows and cells, the frame of its crescents (its wind turned, wx, wz),
// how tall they may be, where they lie at the point (here: all of them, or by the host).
struct Frame {
    dune_phase::Phase rows;
    dune_phase::Phase cells;
    double rowsCoherent = 0.0; // their fades around the defects of their phases
    double cellsCoherent = 0.0;
    double tall = 0.0;
    double wx = 1.0;
    double wz = 0.0;
    dmath::SineCosine turn;
    double groups = 0.0;
    Lie here;
};

// The crescent of the row back (-1, 0, 1) of the frame at the point.
double crescentOf(const Ergs::Impl& m, const dune_phase::Shared& s, const Layer& l, const Frame& f,
                  int back, const Host* host) {
    dune_forms::Barchan b;
    b.t = f.rows.fraction + back;
    b.wavelength = f.rows.wavelength;
    b.across = f.cells.fraction;
    b.cell = f.cells.wavelength;
    b.single = true;
    b.height = f.tall;
    // Whether the crescent of this row may stand here at all, as wide as any (its noises read
    // only then).
    b.presence = 1.0;
    b.width = kWidest;
    if (!dune_forms::barchanMayStand(b, kCrestLow, kCrestLow + kCrestRange)) {
        return 0.0;
    }
    const double along = (f.rows.fraction + back - 0.6) * f.rows.wavelength;
    const double lateral = (f.cells.fraction - 0.5) * f.cells.wavelength;
    Lie lie = f.here;
    double ahead = -1.0;
    double behind = -1.0;
    if (host != nullptr) {
        double share = 0.0;
        double cover = 0.0;
        host->at(host->context, -along * f.wx + lateral * f.wz, -along * f.wz - lateral * f.wx,
                 share, cover, ahead, behind);
        lie = lieOf(host->shareByCover ? share * cover : share, cover, f.groups);
        if (!(lie.share > 0.0)) {
            return 0.0;
        }
    }
    const double shift = kShift * static_cast<double>(l.salt);
    const double cx = s.x[0] - along * f.wx + lateral * f.wz + shift;
    const double cz = s.z[0] - along * f.wz - lateral * f.wx - shift;
    ErgMemos& memos = memosOf(m);
    const auto at = static_cast<std::size_t>(((l.salt / 4) % 4) * 6 + static_cast<std::uint64_t>(2 * (back + 1)));
    const double n = static_cast<double>(m.scatter.fbm2D(cx, cz, memos.crescents[at]));
    const double o =
        static_cast<double>(m.scatter.fbm2D(cx + shift, cz + shift, memos.crescents[at + 1]));
    // Its size and whether it is there both shrink it whole (its width, its height with it:
    // never more than a fifth of its half width): flattened alone, a small or fading crescent
    // was a fine thread of a slip face on smooth ground, with no dune around it. Where the
    // form below wants them, more of them (kCoverOccupancy), mostly big ones (they cover its
    // flanks).
    const double cover = std::max(lie.cover, 0.0);
    const double occupancy = kOccupancy + kGroupOccupancy * lie.group + kCoverOccupancy * cover;
    const double sizePower = kSizePower + (kCoverSizePower - kSizePower) * cover;
    const double size =
        kSmallest + (1.0 - kSmallest) * dmath::pow(std::clamp(0.5 + o, 0.0, 1.0), sizePower);
    const double there = ramp(-kPresenceSoft, kPresenceSoft, occupancy - (0.5 + 1.6 * n));
    b.presence = lie.share * f.rowsCoherent * f.cellsCoherent;
    b.width = kWidest * size * there;
    b.crest = kCrestLow + kCrestRange * std::clamp(0.5 + n, 0.0, 1.0);
    if (ahead >= 0.0) {
        // Its middle 0.6 of its row downwind of the start of it, its row turned from the wind.
        b.width = std::min(b.width, dune_forms::barchanWidthWithin(b, 0.6 * b.wavelength, ahead,
                                                                   behind, f.turn.cosine,
                                                                   f.turn.sine));
    }
    return dune_forms::barchan(b);
}

// The layer, its crescents lying by point (the same at the point for all of them), else by the
// host at their middles.
double layerOf(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
               const Layer& l, double height, const Lie& point, const Host* host) {
    Frame f;
    f.groups = ramp(kVoidLow, kVoidHigh,
                    static_cast<double>(m.clusters.fbm2D(s.x[0], s.z[0], memosOf(m).clusters)));
    f.here = host == nullptr ? lieOf(point.share, point.cover, f.groups) : Lie{1.0, 1.0, 0.0};
    if (!(f.here.share > 0.0)) {
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
    f.rows = both[0];
    f.cells = both[1];
    f.rowsCoherent = ramp(kCoherenceLow, kCoherenceHigh, f.rows.coherence);
    f.cellsCoherent = ramp(kCoherenceLow, kCoherenceHigh, f.cells.coherence);
    if (!(f.here.share * f.rowsCoherent * f.cellsCoherent > 0.0)) {
        return 0.0;
    }
    f.tall = l.base + l.share * std::max(0.0, height);
    const std::array<double, 2> wind = windOf(m, s.x[0], s.z[0]);
    f.turn = dmath::sinCosTurns(l.turn);
    f.wx = wind[0] * f.turn.cosine - wind[1] * f.turn.sine;
    f.wz = wind[0] * f.turn.sine + wind[1] * f.turn.cosine;
    double best = 0.0;
    for (int back = -1; back <= 1; ++back) {
        best = std::max(best, crescentOf(m, s, l, f, back, host));
    }
    return best;
}

} // namespace

double layer(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
             const Layer& l, double height, double share, double cover) {
    if (!(share > 0.0)) {
        return 0.0;
    }
    Lie point;
    point.share = share;
    point.cover = cover;
    return layerOf(m, s, p, l, height, point, nullptr);
}

double layer(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
             const Layer& l, double height, const Host& host) {
    return layerOf(m, s, p, l, height, Lie{}, &host);
}

} // namespace terrain::dune_layers
