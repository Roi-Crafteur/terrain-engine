// The dunes of an erg (ergs_impl.hpp): the kinds of dunes on the continuous phase of their crests
// (dune_phase.hpp), blended by a power mean of their heights (exponent 4), so that the crests
// stay sharp where two kinds meet.
#include "dune_forms.hpp"
#include "dune_phase.hpp"
#include "ergs_impl.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace terrain::dune_field {

namespace {

// Heights: kHeightLow to kHeightLow + kHeightRange (sand^1.5), times kAmpBase + kAmpRange noise,
// within kHeightLow and kHeightHigh.
constexpr double kHeightLow = 20.0;
constexpr double kHeightRange = 280.0;
constexpr double kHeightHigh = 300.0;
constexpr double kAmpBase = 0.92;
constexpr double kAmpRange = 0.25;
// The bare floor between the dunes: kGapLow of the spacing where the sand is plentiful, kGapLow +
// kGapRange where it is scarce.
constexpr double kGapLow = 0.06;
constexpr double kGapRange = 0.35;
// The small dunes on the backs of the draas: spacing, height, angle to the wind, and the meters
// they keep clear of the crest and the slip face of the draa.
constexpr double kSmallSpacing = 320.0;
constexpr double kSmallHeightBase = 6.0;
constexpr double kSmallHeightShare = 0.04;
constexpr double kSmallTurn = 0.07;
constexpr double kSmallClear = 60.0;
constexpr double kSmallGap = 0.15;
// Seifs: their spacing over that of the ridges, their width at most at their base (share of the
// spacing), their ends and the side of their slip faces.
constexpr double kSeifSpacing = 1.1;
constexpr double kSeifWidth = 0.8;
constexpr float kEndsLow = -0.35f;
constexpr float kEndsHigh = 0.1f;
constexpr float kSlipSoft = 0.25f;
// Barchans: their cells across the wind (share of the spacing), their presence (an occupancy
// against a noise) and where their crest lies along the row.
constexpr double kBarchanCell = 0.75;
constexpr double kBarchanOccupancy = 0.85;
constexpr float kPresenceSoft = 0.12f;
constexpr double kScatterDraw = 1.6;   // the draw of a barchan: 0.5 + this times the scatter noise
constexpr double kScatterHeight = 0.5; // the shortest barchans: this share of the height
constexpr double kCrestBase = 0.55;
constexpr double kCrestRange = 0.3;
constexpr double kScatterShift = 7'777.0; // meters: an independent sample of the scatter noise
// Stars: their lattice, their height (the tallest dunes: at least kStarLeast) and their
// occupancy.
constexpr double kStarCell = 2048.0;
constexpr double kStarLeast = 150.0;
constexpr double kStarHigh = 250.0;
constexpr double kStarOccupancy = 0.35;
constexpr double kStarOccupancySand = 0.4;
// Around the defects of the phase (two crests joining), the dunes fade between these coherences.
constexpr float kCoherenceLow = 0.03f;
constexpr float kCoherenceHigh = 0.3f;
// The dunes fade out over the first kEdgeFade of the mask.
constexpr float kEdgeFade = 0.2f;

double ramp(float e0, float e1, double v) {
    return static_cast<double>(noise::smoothstep(e0, e1, static_cast<float>(v)));
}

// What the kinds of one point read.
struct Point {
    double x = 0.0;
    double z = 0.0;
    double height = 0.0;
    double sand = 0.0;
    double mask = 0.0;
    double amplitude = 0.0; // the noise of the heights, about [-1, 1]
};

double powerOf(double h, float weight) {
    return static_cast<double>(weight) * dmath::powInteger(h, 4);
}

// The draa: a ridge carrying small oblique dunes on its back.
double draaAt(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
              const dune_phase::Phase& ridge, double main, double height) {
    dune_phase::Kind small;
    small.turn = kSmallTurn;
    small.fixedSpacing = kSmallSpacing;
    small.salt = 7;
    const dune_phase::Phase phase = dune_phase::phaseAt(m, s, p, small);
    const double lee = height / dune_forms::kLeeTan;
    const double down = ridge.fraction * ridge.wavelength;
    const double up = (1.0 - ridge.fraction) * ridge.wavelength;
    const double on = ramp(static_cast<float>(lee), static_cast<float>(lee + kSmallClear), down) *
                      ramp(0.0f, static_cast<float>(kSmallClear), up) *
                      ramp(kCoherenceLow, kCoherenceHigh, phase.coherence);
    const double tall = kSmallHeightBase + kSmallHeightShare * height;
    return main + on * dune_forms::transverse(phase.fraction, phase.wavelength, tall, kSmallGap,
                                              phase.nominal);
}

// The barchans of the rows of the ridges, in the cells of a phase across the wind.
double barchanAt(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
                 const dune_phase::Phase& ridge, const Point& q) {
    dune_phase::Kind cells;
    cells.across = true;
    cells.spacing = kBarchanCell;
    cells.salt = 11;
    cells.drift = -1.3;
    const dune_phase::Phase across = dune_phase::phaseAt(m, s, p, cells);
    dune_forms::Barchan b;
    b.t = ridge.fraction;
    b.wavelength = ridge.wavelength;
    b.across = across.fraction;
    b.cell = across.wavelength;
    // Which barchans are there and how tall: a noise at their own scale (clusters and gaps,
    // never a lattice), the occupancy thinning toward the edge of the erg.
    const double scatter = static_cast<double>(m.scatter.fbm2D(q.x, q.z, memosOf(m).scatter));
    const double occupancy = kBarchanOccupancy * q.mask;
    const double draw = 0.5 + kScatterDraw * scatter;
    b.presence = ramp(-kPresenceSoft, kPresenceSoft, occupancy - draw) *
                 ramp(kCoherenceLow, kCoherenceHigh, across.coherence);
    b.height = q.height * (kScatterHeight + (1.0 - kScatterHeight) * (0.5 + scatter));
    // Their crests shifted along the rows by the same noise elsewhere: no rank of crescents.
    b.crest = kCrestBase + kCrestRange * static_cast<double>(m.scatter.fbm2D(
                                             q.z + kScatterShift, q.x - kScatterShift));
    return dune_forms::barchan(b);
}

// The seifs: ridges along the wind, on a phase across it.
double seifAt(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
              const Point& q) {
    dune_phase::Kind lines;
    lines.across = true;
    lines.spacing = kSeifSpacing;
    lines.salt = 3;
    const dune_phase::Phase line = dune_phase::phaseAt(m, s, p, lines);
    const double offset = (line.fraction - 0.5) * line.wavelength;
    const double room = kSeifWidth * line.wavelength /
                        (1.0 / dune_forms::kLeeTan + 1.0 / dune_forms::kGentleTan);
    const double top = std::min(q.height, room) *
                       ramp(kEndsLow, kEndsHigh, static_cast<double>(m.ends.fbm2D(q.x, q.z, memosOf(m).ends))) *
                       ramp(kCoherenceLow, kCoherenceHigh, line.coherence);
    const double slip = ramp(-kSlipSoft, kSlipSoft, static_cast<double>(m.side.fbm2D(q.x, q.z, memosOf(m).side)));
    return dune_forms::seif(offset, top, slip);
}

// The kinds that follow the wind, around the pivots.
double windKinds(const Ergs::Impl& m, const Point& q, const FormWeights& w) {
    const dune_phase::Shared s = dune_phase::sharedAt(m, q.x, q.z);
    const dune_phase::Pivots p = dune_phase::pivotsAt(m, s);
    double sum = 0.0;
    const dune_phase::Phase ridge = dune_phase::phaseAt(m, s, p, {});
    const double gap = kGapLow + kGapRange * (1.0 - q.sand);
    const double coherent = ramp(kCoherenceLow, kCoherenceHigh, ridge.coherence);
    if (w.transverse > 0.0f || w.draa > 0.0f) {
        const double main =
            dune_forms::transverse(ridge.fraction, ridge.wavelength, q.height * coherent, gap,
                                   ridge.nominal);
        sum += powerOf(main, w.transverse);
        if (w.draa > 0.0f) {
            sum += powerOf(draaAt(m, s, p, ridge, main, q.height * coherent), w.draa);
        }
    }
    if (w.barchan > 0.0f) {
        sum += powerOf(barchanAt(m, s, p, ridge, q) * coherent, w.barchan);
    }
    if (w.seif > 0.0f) {
        sum += powerOf(seifAt(m, s, p, q), w.seif);
    }
    return sum;
}

double starKind(const Ergs::Impl& m, const Point& q) {
    dune_forms::StarLattice lattice;
    lattice.cell = kStarCell;
    lattice.periodX = m.world.x;
    lattice.periodZ = m.world.z;
    lattice.height = std::clamp(q.height, kStarLeast, kStarHigh);
    lattice.occupancy = kStarOccupancy + kStarOccupancySand * q.sand;
    lattice.key = noise::deriveSeed(m.seed, 0x5354ULL);
    return dune_forms::stars(lattice, q.x, q.z);
}

} // namespace

FormWeights weightsOf(const ErgPoint& e) {
    FormWeights w;
    w.star = noise::smoothstep(0.54f, 0.58f, e.variability);
    w.seif = noise::smoothstep(0.36f, 0.4f, e.variability) * (1.0f - w.star);
    const float rest = 1.0f - w.star - w.seif;
    const float barchan = 1.0f - noise::smoothstep(0.28f, 0.36f, e.sand);
    const float draa = noise::smoothstep(0.8f, 0.88f, e.sand);
    w.barchan = rest * barchan;
    w.draa = rest * draa;
    w.transverse = std::max(0.0f, rest - w.barchan - w.draa);
    return w;
}

double heightOf(double sand, double amplitude) {
    return std::clamp((kHeightLow + kHeightRange * sand * std::sqrt(sand)) *
                          (kAmpBase + kAmpRange * amplitude),
                      kHeightLow, kHeightHigh);
}

float dunes(const Ergs::Impl& m, double x, double z, const ErgPoint& e) {
    if (!(e.mask > 0.0f)) {
        return 0.0f;
    }
    Point q;
    q.x = x;
    q.z = z;
    q.sand = static_cast<double>(e.sand);
    q.mask = static_cast<double>(e.mask);
    q.amplitude = static_cast<double>(m.amplitude.fbm2D(x, z, memosOf(m).amplitude));
    q.height = heightOf(q.sand, q.amplitude);
    const FormWeights w = weightsOf(e);
    double sum = 0.0;
    if (w.star < 1.0f) {
        sum += windKinds(m, q, w);
    }
    if (w.star > 0.0f) {
        sum += powerOf(starKind(m, q), w.star);
    }
    const double h = std::sqrt(std::sqrt(sum));
    return static_cast<float>(h * static_cast<double>(noise::smoothstep(0.0f, kEdgeFade, e.mask)));
}

} // namespace terrain::dune_field
