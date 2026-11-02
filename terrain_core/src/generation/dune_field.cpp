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
constexpr double kSeifSpacing = 1.4;
constexpr double kSeifWidth = 0.8;
constexpr float kEndsLow = -0.35f;
constexpr float kEndsHigh = 0.1f;
constexpr float kSlipSoft = 0.25f;
constexpr double kSeifSinuous = 2.2;  // their crests wander more than the transverse ones
constexpr double kPlinthHeight = 0.45; // the plinth under their crest, share of their height
constexpr double kPlinthWidth = 0.38;  // and its half width, share of their spacing
constexpr double kSeifSand = 0.5;      // their height at least that of this sand
// The second system of seifs (the branches): its spacing over the first, where it lies (a noise
// sampled elsewhere) and how much of it.
constexpr double kBranchSpacing = 1.27;
constexpr double kBranchShift = 3'333.0;
constexpr float kBranchLow = -0.1f;
constexpr float kBranchHigh = 0.25f;
// Barchans: their cells across the wind (share of the spacing), their presence (an occupancy
// against a noise) and where their crest lies along the row.
constexpr double kBarchanCell = 0.75;
constexpr double kBarchanOccupancy = 0.85;
constexpr float kPresenceSoft = 0.35f;
constexpr double kScatterDraw = 1.6;   // the draw of a barchan: 0.5 + this times the scatter noise
constexpr double kScatterHeight = 0.5; // the shortest barchans: this share of the height
constexpr double kBarchanLeast = 30.0;
constexpr double kBarchanMost = 60.0;
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
    // Barchans stand about a tenth as high as they are wide (dune_forms: their width caps them),
    // whatever the little sand around them: kBarchanLeast to kBarchanMost meters at most.
    b.height = std::clamp(2.0 * q.height, kBarchanLeast, kBarchanMost) *
               (kScatterHeight + (1.0 - kScatterHeight) * (0.5 + scatter));
    // Their crests shifted along the rows by the same noise elsewhere: no rank of crescents.
    b.crest = kCrestBase + kCrestRange * static_cast<double>(m.scatter.fbm2D(
                                             q.z + kScatterShift, q.x - kScatterShift));
    return dune_forms::barchan(b);
}

// The phase of a system of seifs: across the wind, sinuous.
dune_phase::Kind seifKind(double spacing, std::uint64_t salt) {
    dune_phase::Kind lines;
    lines.across = true;
    lines.spacing = spacing;
    lines.salt = salt;
    lines.sinuous = kSeifSinuous;
    return lines;
}

// The seifs of a system: ridges along the wind on the phase line across it.
double seifLine(const Point& q, const dune_phase::Phase& line, double ends, double slip) {
    const double offset = (line.fraction - 0.5) * line.wavelength;
    const double room = kSeifWidth * line.wavelength /
                        (1.0 / dune_forms::kLeeTan + 1.0 / dune_forms::kGentleTan);
    // Seifs are big dunes: at least the height of a middling sand.
    const double tall = std::max(q.height, heightOf(kSeifSand, q.amplitude));
    const double top = std::min(tall, room) * ramp(kEndsLow, kEndsHigh, ends) *
                       ramp(kCoherenceLow, kCoherenceHigh, line.coherence);
    // A sharp crest on a broad, gentle plinth: a ridge, not a line.
    const double plinth =
        kPlinthHeight * top *
        (1.0 - dune_forms::hermite(std::abs(offset) / (kPlinthWidth * line.wavelength)));
    return std::max(dune_forms::seif(offset, top, slip), plinth);
}

// The seifs: ridges along the wind, on a phase across it, sinuous; a second system of another
// spacing over part of the erg joins and splits them (Y junctions). Both systems read the same
// weights of the pivots (dune_phase::phasesAt). Not only where the seifs dominate: where they mix
// with the stars (seif weight about 0.25) the power mean keeps them, branches and all.
double seifAt(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
              const Point& q) {
    ErgMemos& memo = memosOf(m);
    const double ends = static_cast<double>(m.ends.fbm2D(q.x, q.z, memo.ends));
    const double slip =
        ramp(-kSlipSoft, kSlipSoft, static_cast<double>(m.side.fbm2D(q.x, q.z, memo.side)));
    const dune_phase::Kind first = seifKind(kSeifSpacing, 3);
    const double share =
        ramp(kBranchLow, kBranchHigh,
             static_cast<double>(
                 m.ends.fbm2D(q.z + kBranchShift, q.x - kBranchShift, memo.branches)));
    if (!(share > 0.0)) {
        return seifLine(q, dune_phase::phaseAt(m, s, p, first), ends, slip);
    }
    const std::array<dune_phase::Phase, 2> lines =
        dune_phase::phasesAt(m, s, p, first, seifKind(kBranchSpacing * kSeifSpacing, 5));
    return std::max(seifLine(q, lines[0], ends, slip), share * seifLine(q, lines[1], ends, slip));
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

namespace {

// The means of the kinds over their pattern, closed forms of dune_forms (for the far horizon):
// a transverse ridge holds height (1 - gap) / 2 on average, whatever its slip face; a barchan
// about kBarchanVolume half top^2 in its cell; a seif top^2 (cot 33 + cot 22) / 2 per spacing; a
// star a cone with its arms. kCoherentMean: the share the defects of the phase leave.
constexpr double kCoherentMean = 0.9;
constexpr double kBarchanVolume = 6.82;
constexpr double kBarchanTop = 0.75;   // the mean height of a barchan over its tallest
constexpr double kBarchanHalfShare = 0.26; // dune_forms: their half width over their cell
constexpr double kSeifEnds = 0.44;     // the share of the length of the seifs their ends leave
constexpr double kStarArms = 1.3;      // the volume of a star over that of its cone
constexpr double kPi = 3.141592653589793;

struct Means {
    double transverse = 0.0;
    double draa = 0.0;
    double barchan = 0.0;
    double seif = 0.0;
    double star = 0.0;
};

Means meansOf(const ErgPoint& e, double height, double spacing) {
    Means out;
    const double sand = static_cast<double>(e.sand);
    const double gap = kGapLow + kGapRange * (1.0 - sand);
    const double room = spacing * (1.0 - gap);
    const double tall = std::min(height, room / (1.0 / dune_forms::kLeeTan + 7.0));
    out.transverse = kCoherentMean * tall * (1.0 - gap) / 2.0;
    const double small = kSmallHeightBase + kSmallHeightShare * tall;
    const double on = std::max(0.0, room - tall / dune_forms::kLeeTan - 2.0 * kSmallClear) / spacing;
    out.draa = out.transverse + kCoherentMean * on * small * (1.0 - kSmallGap) / 2.0;
    const double cell = kBarchanCell * spacing;
    const double half = kBarchanHalfShare * cell;
    const double top =
        kBarchanTop * std::min(std::clamp(2.0 * height, kBarchanLeast, kBarchanMost), half / 4.5);
    const double there = std::clamp((kBarchanOccupancy * static_cast<double>(e.mask) - 0.5) /
                                        (2.0 * kScatterDraw * 0.25) + 0.5,
                                    0.0, 1.0);
    out.barchan = there * kBarchanVolume * half * top * top / (cell * spacing);
    const double seifSpacing = kSeifSpacing * spacing;
    const double cot = 1.0 / dune_forms::kLeeTan + 1.0 / dune_forms::kGentleTan;
    const double seifTop =
        std::min(std::max(height, heightOf(kSeifSand, 0.0)), kSeifWidth * seifSpacing / cot);
    out.seif = kSeifEnds * (seifTop * seifTop * cot / 2.0 / seifSpacing +
                            kPlinthHeight * kPlinthWidth * seifTop);
    const double peak = std::clamp(height, kStarLeast, kStarHigh);
    const double radius = peak / dune_forms::kStarTan;
    out.star = (kStarOccupancy + kStarOccupancySand * sand) * kStarArms * kPi * radius * radius *
               peak / 3.0 / (kStarCell * kStarCell);
    return out;
}

} // namespace

double meanDunes(const Ergs::Impl& m, double x, double z, const ErgPoint& e) {
    if (!(e.mask > 0.0f)) {
        return 0.0;
    }
    const dune_phase::Shared s = dune_phase::sharedAt(m, x, z);
    const double spacing = dune_phase::nominalAt(m, s, dune_phase::pivotsAt(m, s));
    const double amplitude = static_cast<double>(m.amplitude.fbm2D(x, z, memosOf(m).amplitude));
    const Means k = meansOf(e, heightOf(static_cast<double>(e.sand), amplitude), spacing);
    const FormWeights w = weightsOf(e);
    const double mean = static_cast<double>(w.transverse) * k.transverse +
                        static_cast<double>(w.draa) * k.draa +
                        static_cast<double>(w.barchan) * k.barchan +
                        static_cast<double>(w.seif) * k.seif + static_cast<double>(w.star) * k.star;
    return mean * static_cast<double>(noise::smoothstep(0.0f, kEdgeFade, e.mask));
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
