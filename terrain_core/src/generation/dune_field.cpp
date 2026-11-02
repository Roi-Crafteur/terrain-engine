// The dunes of an erg (ergs_impl.hpp): the kinds of dunes on the continuous phase of their crests
// (dune_phase.hpp), blended by a power mean of their heights (exponent 4), so that the crests
// stay sharp where two kinds meet.
#include "dune_forms.hpp"
#include "dune_layers.hpp"
#include "dune_phase.hpp"
#include "dune_waves.hpp"
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
// The troughs between the ridges and the chains sink this share of their height below the floor.
constexpr double kDig = 0.4;
constexpr double kDigStart = 0.5;   // of the half width of the plinth of the chains
constexpr double kDigSpacing = 0.08;
constexpr double kSmallGap = 0.15;
// Seifs: their spacing over that of the ridges, their width at most at their base (share of the
// spacing), their ends and the side of their slip faces.
constexpr double kSeifSpacing = 1.4;
constexpr double kSeifWidth = 0.8;
constexpr float kEndsLow = -0.35f;
constexpr float kEndsHigh = 0.1f;
constexpr float kSlipSoft = 0.25f;
constexpr double kSeifSinuous = 1.2;  // their crests wander a little more than the ridges
constexpr double kPlinthHeight = 0.45; // the plinth under their crest, share of their height
constexpr double kPlinthWidth = 0.38;  // and its half width, share of their spacing
constexpr double kSeifSand = 0.7;      // their height at least that of this sand
// The second system of seifs (the branches): its spacing over the first, where it lies (a noise
// sampled elsewhere) and how much of it.
constexpr double kBranchSpacing = 1.27;
constexpr double kBranchShift = 3'333.0;
constexpr float kBranchLow = -0.1f;
constexpr float kBranchHigh = 0.25f;
// The stars on the chains: their lattice, their height over the chain (share of its height),
// how many cells hold one, and how high on the chain their center must stand (within
// kOnChainSoft).
constexpr double kChainStarCell = 2'600.0;
constexpr double kChainStarShare = 0.65;
constexpr double kChainStarOccupancy = 0.75;
constexpr double kOnChain = 0.6;
constexpr double kOnChainSoft = 0.1;
constexpr float kChainCoherenceLow = 0.15f;
constexpr float kChainCoherenceHigh = 0.45f;
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
// The crests of the ridges break in saddles where the noise of the sides (sampled kBreakShift
// away, 3 km) is low, about a fifth of their length below half their height: no crest runs on
// for tens of kilometers (a straight band to the horizon, seen along it).
constexpr float kBreakLow = -0.45f;
constexpr float kBreakHigh = 0.05f;
constexpr double kBreakShift = 5'555.0;
// The breaks fade out from kBreakFull to kBreakNone meters of height: lowering a tall crest over
// a few hundred meters would make it steep along its length (the draas keep their long ridges
// and their small dunes); the crest falls by at most about 22 degrees along it.
constexpr double kBreakFull = 80.0;
constexpr double kBreakNone = 180.0;
constexpr double kBreakMean = 0.78; // the mean of the ramp of the breaks over the noise (measured)
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

// The kinds blended: the power mean of their heights above the floor (exponent 4, the crests stay
// sharp where two kinds meet) weighted by the fourth powers of their weights, times the sum of the
// weights; the troughs below it by their weights. A kind taking a share w of the weight rises by
// about w of its height: not w^(1/4), a step where it begins.
struct Blend {
    double above = 0.0;  // the sum of the weights^4 times the heights^4
    double weights = 0.0; // the sum of the weights^4
    double total = 0.0;  // the sum of the weights
    double below = 0.0;  // the sum of the weights times the depths (negative)
    void add(double h, float weight) {
        const auto w = static_cast<double>(weight);
        const double w4 = dmath::powInteger(w, 4);
        above += w4 * dmath::powInteger(std::max(h, 0.0), 4);
        weights += w4;
        total += w;
        below += w * std::min(h, 0.0);
    }
    void scale(double k) {
        const double k4 = dmath::powInteger(k, 4);
        above *= k4;
        weights *= k4;
        total *= k;
        below *= k;
    }
    [[nodiscard]] double value() const {
        return weights > 0.0 ? total * std::sqrt(std::sqrt(above / weights)) + below : below;
    }
};

// The ridges carry dunes on their backs (dune_layers): oblique ridges in segments, about
// kSmallSpacing meters apart, clear of their slip face and of their crest.
double layered(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
               const dune_phase::Phase& ridge, double main, double height) {
    const double lee = height / dune_forms::kLeeTan;
    const double down = ridge.fraction * ridge.wavelength;
    const double up = (1.0 - ridge.fraction) * ridge.wavelength;
    const double on = ramp(static_cast<float>(lee), static_cast<float>(lee + kSmallClear), down) *
                      ramp(0.0f, static_cast<float>(kSmallClear), up);
    const dune_layers::Layer medium{kSmallSpacing, kSmallHeightBase, kSmallHeightShare, kSmallTurn, 7};
    return main + dune_layers::layer(m, s, p, medium, height, on);
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
    const double spacing = line.wavelength;
    const double offset = (line.fraction - 0.5) * spacing;
    const double room = kSeifWidth * line.nominal /
                        (1.0 / dune_forms::kLeeTan + 1.0 / dune_forms::kGentleTan);
    // Seifs are big dunes: at least the height of a middling sand.
    const double tall = std::max(q.height, heightOf(kSeifSand, q.amplitude));
    // They fade where their phase tears (low coherence) or where its local spacing strays from
    // the nominal one (there it would bend their flanks past the angle of repose), and at their
    // ends: the whole form scaled down, so that its fading never adds to the slope of its flanks.
    const double ratio = line.wavelength / line.nominal;
    const double fade = ramp(kEndsLow, kEndsHigh, ends) *
                        ramp(kChainCoherenceLow, kChainCoherenceHigh, line.coherence) *
                        ramp(0.45f, 0.85f, ratio) * (1.0 - ramp(1.4f, 2.4f, ratio));
    if (!(fade > 0.0)) {
        return 0.0;
    }
    const double top = std::min(tall, room);
    // A sharp crest on a broad, gentle plinth: a ridge, not a line.
    const double plinth =
        kPlinthHeight * top *
        (1.0 - dune_forms::hermite(std::abs(offset) / (kPlinthWidth * spacing)));
    // The corridor between two chains sinks kDig of their height (at most kDigSpacing of their
    // spacing), from the middle of the plinth to the middle of the corridor: gently enough.
    const double edge = kDigStart * kPlinthWidth * spacing;
    const double dig = std::min(kDig * top, kDigSpacing * spacing) *
                       dune_forms::hermite((std::abs(offset) - edge) / (0.5 * spacing - edge));
    return fade * (std::max(dune_forms::seif(offset, top, slip), plinth) - dig);
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
    // The branches rise over the first system by their share (both dig corridors: a share of a
    // corridor would lift it in a step where the share begins).
    const double main = seifLine(q, lines[0], ends, slip);
    return main + share * std::max(0.0, seifLine(q, lines[1], ends, slip) - main);
}

// How much the crests of ridges of a height break, [0, 1].
double breakStrength(double height) {
    return std::clamp((kBreakNone - height) / (kBreakNone - kBreakFull), 0.0, 1.0);
}

// The share of the height the crests of the transverse ridges keep at q (their saddles).
double unbroken(const Ergs::Impl& m, const Point& q) {
    const double strength = breakStrength(q.height);
    if (!(strength > 0.0)) {
        return 1.0;
    }
    const double n = static_cast<double>(
        m.side.fbm2D(q.x + kBreakShift, q.z - kBreakShift, memosOf(m).breaks));
    return 1.0 - strength * (1.0 - ramp(kBreakLow, kBreakHigh, n));
}

// The chains with their stars: a star of the lattice of kChainStarCell stands on a chain where
// the chain at its center reaches kOnChain of its height, rising from it (dune_forms::starsOn);
// the higher of the chain and the stars (their slopes never add up).
double chainAt(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
               const Point& q) {
    const double chain = seifAt(m, s, p, q);
    const double top = std::max(q.height, heightOf(kSeifSand, q.amplitude));
    dune_forms::StarLattice lattice;
    lattice.cell = kChainStarCell;
    lattice.periodX = m.world.x;
    lattice.periodZ = m.world.z;
    lattice.height = kChainStarShare * top;
    lattice.occupancy = kChainStarOccupancy;
    lattice.key = noise::deriveSeed(m.seed, 0x43535452ULL);
    // The chain lies between -kDig and 1 times top: a star ends under its lowest ground.
    lattice.depth = (1.0 + kDig) * top;
    const double stars = dune_forms::starsOn(lattice, q.x, q.z, [&](double cx, double cz) {
        const dune_phase::Shared sc = dune_phase::sharedAt(m, cx, cz);
        Point qc = q;
        qc.x = cx;
        qc.z = cz;
        dune_forms::Pedestal under;
        under.base = seifAt(m, sc, dune_phase::pivotsAt(m, sc), qc);
        under.presence = ramp(static_cast<float>(kOnChain - kOnChainSoft),
                              static_cast<float>(kOnChain + kOnChainSoft), under.base / top);
        return under;
    });
    return std::max(chain, stars);
}

// The kinds that follow the wind, around the pivots.
void windKinds(const Ergs::Impl& m, const dune_phase::Shared& s, const Point& q,
               const FormWeights& w, Blend& sum) {
    const dune_phase::Pivots p = dune_phase::pivotsAt(m, s);
    const dune_phase::Phase ridge = dune_phase::phaseAt(m, s, p, {});
    const double gap = kGapLow + kGapRange * (1.0 - q.sand);
    const double coherent = ramp(kCoherenceLow, kCoherenceHigh, ridge.coherence);
    if (w.transverse > 0.0f || w.draa > 0.0f) {
        const double tall = q.height * coherent * unbroken(m, q);
        const double main = layered(
            m, s, p, ridge,
            dune_forms::transverse(ridge.fraction, ridge.wavelength, tall, gap, ridge.nominal, kDig),
            tall);
        sum.add(main, w.transverse + w.draa);
    }
    if (w.barchan > 0.0f) {
        sum.add(barchanAt(m, s, p, ridge, q) * coherent, w.barchan);
    }
    if (w.seif > 0.0f) {
        sum.add(chainAt(m, s, p, q), w.seif);
    }
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

// The realistic ergs on the model of the Grand Erg Oriental (NASA Earth Observatory): linear
// chains with star dunes on them and smooth corridors between them (the seifs, kind Seif), the
// barchans where the sand is scarce, the free stars where the winds come from every side.
FormWeights weightsOf(const ErgPoint& e) {
    FormWeights w;
    w.star = noise::smoothstep(0.54f, 0.58f, e.variability);
    const float rest = 1.0f - w.star;
    w.barchan = rest * (1.0f - noise::smoothstep(0.02f, 0.38f, e.sand));
    w.seif = std::max(0.0f, rest - w.barchan);
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
constexpr double kSeifEnds = 0.56; // the share their ends, defects and stars leave (measured)
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
    // Their saddles keep kBreakMean of the height on average (the ridges small enough to break).
    out.transverse = kCoherentMean * tall * (1.0 - gap) / 2.0 *
                     (1.0 - breakStrength(height) * (1.0 - kBreakMean));
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
    // The corridors: the dig rises along a Hermite curve over the outer part of each half
    // spacing (seifLine), half of it on average.
    const double corridor = std::min(kDig * seifTop, kDigSpacing * seifSpacing) * 0.5 *
                            (1.0 - 2.0 * kDigStart * kPlinthWidth);
    out.seif = kSeifEnds * (seifTop * seifTop * cot / 2.0 / seifSpacing +
                            kPlinthHeight * kPlinthWidth * seifTop - corridor);
    const double peak = std::clamp(height, kStarLeast, kStarHigh);
    const double radius = peak / dune_forms::kStarTan;
    out.star = (kStarOccupancy + kStarOccupancySand * sand) * kStarArms * kPi * radius * radius *
               peak / 3.0 / (kStarCell * kStarCell);
    return out;
}

// The share of their height the dunes keep at the edges of the erg: the realistic kinds fade
// over the first kEdgeFade of the mask, the waves (far taller) with the whole mask, whose fades
// stretch with their height (ergs.cpp).
double edgeOf(const ErgPoint& e) {
    const double fade = static_cast<double>(noise::smoothstep(0.0f, kEdgeFade, e.mask));
    const double waves = static_cast<double>(e.waves) + static_cast<double>(e.giants);
    return fade + (static_cast<double>(e.mask) - fade) * waves;
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
    const float realistic = 1.0f - e.waves - e.giants;
    // The means blended as the heights are (Blend): the kinds mixed keep more than their share.
    Blend sum;
    sum.add(k.transverse, realistic * w.transverse);
    sum.add(k.draa, realistic * w.draa);
    sum.add(k.barchan, realistic * w.barchan);
    sum.add(k.seif, realistic * w.seif);
    sum.add(k.star, realistic * w.star);
    const double sand = static_cast<double>(e.sand);
    if (e.waves > 0.0f) {
        sum.add(dune_waves::meanWaves(m, s, sand, amplitude, false), e.waves);
    }
    if (e.giants > 0.0f) {
        sum.add(dune_waves::meanWaves(m, s, sand, amplitude, true), e.giants);
    }
    return sum.value() * edgeOf(e);
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
    const dune_phase::Shared s = dune_phase::sharedAt(m, x, z);
    // The characters: the realistic kinds, the big waves and the giant waves, by a power mean of
    // their heights weighted by the characters (the realistic alone: the same bits as before).
    const float realistic = 1.0f - e.waves - e.giants;
    Blend sum;
    if (realistic > 0.0f) {
        const FormWeights w = weightsOf(e);
        if (w.star < 1.0f) {
            windKinds(m, s, q, w, sum);
        }
        if (w.star > 0.0f) {
            sum.add(starKind(m, q), w.star);
        }
        sum.scale(static_cast<double>(realistic));
    }
    if (e.waves > 0.0f) {
        sum.add(dune_waves::waves(m, s, x, z, q.sand, q.amplitude, false), e.waves);
    }
    if (e.giants > 0.0f) {
        sum.add(dune_waves::waves(m, s, x, z, q.sand, q.amplitude, true), e.giants);
    }
    return static_cast<float>(sum.value() * edgeOf(e));
}

} // namespace terrain::dune_field
