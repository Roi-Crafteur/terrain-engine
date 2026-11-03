// The dunes of an erg (ergs_impl.hpp): the kinds of dunes on the continuous phase of their crests
// (dune_phase.hpp), blended by a power mean of their heights (exponent 4), so that the crests
// stay sharp where two kinds meet.
#include "dune_blend.hpp"
#include "dune_field_constants.hpp"
#include "dune_forms.hpp"
#include "dune_layers.hpp"
#include "dune_phase.hpp"
#include "dune_waves.hpp"
#include "ergs_impl.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <bit>
#include <array>
#include <cmath>

namespace terrain::dune_field {

namespace {

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
    double windX = 1.0;     // toward which the sand moves (ErgPoint)
    double windZ = 0.0;
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

// The middle of the barchan of a row (back: 0 this one, -1 and 1 those before and after it) over
// q: where the phase of the ridges reaches the base of its crest and that of its cells their
// middle, both taken as linear around q (their cells drift along the wind: from the wind alone,
// the middle would slide along the barchan); from the wind where the phases run near parallel.
std::array<double, 2> middleOf(const dune_phase::Phase& ridge, const dune_phase::Phase& across,
                               int back, const Point& q) {
    const double toRow = kCrestBase - ridge.fraction - back; // cycles of each phase to go
    const double toMiddle = 0.5 - across.fraction;
    const double det = ridge.gx * across.gz - ridge.gz * across.gx;
    if (std::abs(det) > 0.5 * dmath::hypot(ridge.gx, ridge.gz) * dmath::hypot(across.gx, across.gz)) {
        return {q.x + (toRow * across.gz - toMiddle * ridge.gz) / det,
                q.z + (ridge.gx * toMiddle - across.gx * toRow) / det};
    }
    const double along = -toRow * ridge.wavelength;
    const double lateral = -toMiddle * across.wavelength;
    return {q.x - along * q.windX + lateral * q.windZ, q.z - along * q.windZ - lateral * q.windX};
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
    // Each barchan reads its noises at its own middle (estimated from the phases and the wind):
    // its presence, size and height never change over it, which would warp it.
    double best = 0.0;
    for (int back = -1; back <= 1; ++back) {
        const std::array<double, 2> c = middleOf(ridge, across, back, q);
        const double cx = c[0];
        const double cz = c[1];
        dune_forms::Barchan b;
        b.t = ridge.fraction + back;
        b.wavelength = ridge.wavelength;
        b.across = across.fraction;
        b.cell = across.wavelength;
        b.single = true;
        // Which barchans are there and how tall: a noise at their own scale (clusters and gaps,
        // never a lattice), the occupancy thinning toward the edge of the erg; their height
        // (about a tenth of their width, dune_forms), kBarchanLeast to kBarchanMost meters at most
        // whatever the little sand around them, scaled with their presence.
        const double scatter = static_cast<double>(m.scatter.fbm2D(cx, cz));
        const double draw = 0.5 + kScatterDraw * scatter;
        b.presence = ramp(-kPresenceSoft, kPresenceSoft, kBarchanOccupancy * q.mask - draw) *
                     ramp(kCoherenceLow, kCoherenceHigh, across.coherence) *
                     (kScatterHeight + (1.0 - kScatterHeight) * (0.5 + scatter));
        // Each its own size, narrower and lower together (about ten times as wide as high), slid
        // across its cell within the room that leaves: no lattice of equal crescents.
        const double size = std::clamp(
            kSizeMean + 0.5 * kSizeSpread * static_cast<double>(m.sizes.fbm2D(cx, cz)), kSizeLeast,
            1.0);
        const double slide = static_cast<double>(m.sizes.fbm2D(cx + kScatterShift, cz));
        b.width = size;
        b.across = across.fraction -
                   kSlide * (0.5 - kBarchanHalfShare * size) * std::clamp(2.0 * slide, -1.0, 1.0);
        b.height = size * std::clamp(2.0 * q.height, kBarchanLeast, kBarchanMost);
        // Their crests shifted along the rows by the same noise elsewhere: no rank of crescents.
        b.crest = kCrestBase + kCrestRange * std::clamp(2.0 * static_cast<double>(m.sizes.fbm2D(
                                                             cx - kScatterShift, cz)), -1.0, 1.0);
        best = std::max(best, dune_forms::barchan(b));
    }
    return best;
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
    // At most kStretched times the nominal spacing where the phase stretches (dune_waves: the
    // local one varies fast there).
    const double spacing = std::min(line.wavelength, kStretched * line.nominal);
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
                        ramp(0.25f, 0.85f, ratio) * (1.0 - ramp(1.4f, 2.4f, ratio));
    if (!(fade > 0.0)) {
        return 0.0;
    }
    const double crest = std::min(tall, room);
    // A sharp crest on a broad, gentle plinth: a ridge, not a line.
    const double plinth =
        kPlinthHeight * crest *
        (1.0 - dune_forms::hermite(std::abs(offset) / (kPlinthWidth * spacing)));
    // The corridor between two chains sinks kDig of their height (at most kDigSpacing of their
    // spacing), from the middle of the plinth or the foot of their flanks (their slopes never
    // add) to the middle of the corridor, never steeper than kDigTan.
    const double edge = std::max(kDigStart * kPlinthWidth * spacing, crest / dune_forms::kGentleTan);
    const double run = 0.5 * spacing - edge;
    const double dig =
        run > 0.0 ? std::min({kDig * crest, kDigSpacing * spacing, kDigTan * run / 1.5}) *
                        dune_forms::hermite((std::abs(offset) - edge) / run)
                  : 0.0;
    return fade * (std::max(dune_forms::seif(offset, crest, slip), plinth) - dig);
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
                 m.ends.fbm2D(q.x + kBranchShift, q.z - kBranchShift, memo.branches)));
    if (!(share > 0.0)) {
        return seifLine(q, dune_phase::phaseAt(m, s, p, first), ends, slip);
    }
    dune_phase::Kind branches = seifKind(kBranchSpacing * kSeifSpacing, 5);
    branches.sinuous *= kBranchSinuous;
    const std::array<dune_phase::Phase, 2> lines = dune_phase::phasesAt(m, s, p, first, branches);
    // The branches rise over the first system by their share, joined smoothly (a plain maximum
    // drew a straight fold along the wind where they meet: an edge of light and shade over
    // kilometers). Wherever they stand (dune_waves::joined at the crest of its profile): gated
    // near their crests, their flanks of 33 degrees would steepen past 40.
    const double main = seifLine(q, lines[0], ends, slip);
    const double branch = seifLine(q, lines[1], ends, slip);
    return dune_waves::joined(main, branch, 1.0, share,
                              kBranchSoft * std::max(q.height, heightOf(kSeifSand, q.amplitude)));
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

// A star on the chains at (cx, cz): the chain under its center (of a sand of kSeifSand: the
// same whatever the point that looks at it) and its height there.
dune_forms::Pedestal pedestalOf(const Ergs::Impl& m, double cx, double cz) {
    const dune_phase::Shared sc = dune_phase::sharedAt(m, cx, cz);
    Point qc;
    qc.x = cx;
    qc.z = cz;
    qc.sand = kSeifSand;
    qc.amplitude = static_cast<double>(m.amplitude.fbm2D(cx, cz));
    qc.height = heightOf(kSeifSand, qc.amplitude);
    const double top = qc.height;
    dune_forms::Pedestal under;
    under.base = seifAt(m, sc, dune_phase::pivotsAt(m, sc), qc);
    under.presence = ramp(static_cast<float>(kOnChain - kOnChainSoft),
                          static_cast<float>(kOnChain + kOnChainSoft), under.base / top);
    under.height = kChainStarShare * top;
    // The chains around lie between -kDig times the tallest and top: it ends under them.
    under.depth = top + kDig * kHeightHigh;
    return under;
}

// The chains with their stars: a star of the lattice of kChainStarCell stands on a chain where
// the chain at its center reaches kOnChain of its height, rising from it (dune_forms::starsOn);
// the higher of the chain and the stars (their slopes never add up).
double chainAt(const Ergs::Impl& m, const dune_phase::Shared& s, const dune_phase::Pivots& p,
               const Point& q) {
    const double chain = seifAt(m, s, p, q);
    dune_forms::StarLattice lattice;
    lattice.cell = kChainStarCell;
    lattice.periodX = m.world.x;
    lattice.periodZ = m.world.z;
    lattice.height = kChainStarShare * kHeightHigh; // the bounds: the tallest chains
    lattice.occupancy = kChainStarOccupancy;
    lattice.key = noise::deriveSeed(m.seed, 0x43535452ULL);
    lattice.depth = (1.0 + kDig) * kHeightHigh;
    ErgMemos& memo = memosOf(m);
    const double stars = dune_forms::starsOn(lattice, q.x, q.z, [&](double cx, double cz) {
        // Read once per star (they depend on its center only): the last ones met, by center.
        ErgMemos::Star& seen =
            memo.stars[noise::deriveSeed(std::bit_cast<std::uint64_t>(cx),
                                         std::bit_cast<std::uint64_t>(cz)) %
                       memo.stars.size()];
        if (seen.cx == cx && seen.cz == cz) {
            return dune_forms::Pedestal{seen.pedestal[0], seen.pedestal[1], seen.pedestal[2],
                                        seen.pedestal[3]};
        }
        const dune_forms::Pedestal under = pedestalOf(m, cx, cz);
        seen = {cx, cz, {under.base, under.presence, under.height, under.depth}};
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
constexpr double kSeifEnds = 0.62; // the share their ends, defects and stars leave (measured)
constexpr double kStarArms = 1.22;     // the volume of a star over that of its cone (measured)
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
    out.barchan = there * kSizeVolume * kBarchanVolume * half * top * top / (cell * spacing);
    const double seifSpacing = kSeifSpacing * spacing;
    const double cot = 1.0 / dune_forms::kLeeTan + 1.0 / dune_forms::kGentleTan;
    const double seifTop =
        std::min(std::max(height, heightOf(kSeifSand, 0.0)), kSeifWidth * seifSpacing / cot);
    // The corridors: the dig rises along a Hermite curve over the outer part of each half
    // spacing (seifLine), half of it on average.
    const double run = std::max(0.0, 0.5 * seifSpacing -
                                         std::max(kDigStart * kPlinthWidth * seifSpacing,
                                                  seifTop / dune_forms::kGentleTan));
    const double corridor =
        std::min({kDig * seifTop, kDigSpacing * seifSpacing, kDigTan * run / 1.5}) * run /
        seifSpacing;
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

float dunes(const Ergs::Impl& m, double x, double z, const ErgPoint& e, double footprint) {
    if (!(e.mask > 0.0f)) {
        return 0.0f;
    }
    Point q;
    q.x = x;
    q.z = z;
    q.sand = static_cast<double>(e.sand);
    q.mask = static_cast<double>(e.mask);
    q.windX = e.windX;
    q.windZ = e.windZ;
    q.amplitude = static_cast<double>(m.amplitude.fbm2D(x, z, memosOf(m).amplitude));
    q.height = heightOf(q.sand, q.amplitude);
    dune_phase::Shared s = dune_phase::sharedAt(m, x, z);
    s.footprint = footprint;
    // The characters: the realistic kinds, the big waves and the giant waves, by a power mean of
    // their heights weighted by the characters (the realistic alone: the same bits as before).
    const float realistic = 1.0f - e.waves - e.giants;
    Blend sum;
    if (realistic > kMinorLeast) {
        const FormWeights w = weightsOf(e);
        if (w.star < 1.0f) {
            windKinds(m, s, q, w, sum);
        }
        if (w.star > 0.0f) {
            sum.add(starKind(m, q), w.star);
        }
        sum.scale(effectiveWeight(static_cast<double>(realistic)));
    }
    // A character of a weight under kMinorLeast takes none (effectiveWeight): not computed.
    if (e.waves > kMinorLeast) {
        sum.add(dune_waves::waves(m, s, x, z, q.sand, q.amplitude, false), e.waves);
    }
    if (e.giants > kMinorLeast) {
        sum.add(dune_waves::waves(m, s, x, z, q.sand, q.amplitude, true), e.giants);
    }
    return static_cast<float>(sum.value() * edgeOf(e));
}

} // namespace terrain::dune_field
