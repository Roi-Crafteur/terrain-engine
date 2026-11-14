// The rock of the plateaus (agent 46, tableland_relief.hpp: passesAt, topRelief): the strata of
// the walls of the mountains (mountain_walls.hpp) laid parallel to the tops of the plateaus, on
// their taluses and walls; and the hard layers of each wall (its caprock, its great cliff) forced,
// 2 to 4 of them in its height (tableland_cliffs.hpp: layersAt).
#include "tableland_relief.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>

namespace terrain::tableland_relief {

namespace {

// The rock: formations kBandLow to kBandHigh meters thick (Noises::bands), offset by up to kDip
// (Noises::dip: a gentle regional dip, the sequence under the rims not the same everywhere), their
// rims receded by up to kRim (Noises::rims), their faces rising over kRunArid to kRunHumid of a
// band (up to twice that where the rims recede least); kHardArid to kHardHumid of them hard, their
// treads rising kTilt of their band plus up to kSpread of the rest, the foot of their cliffs
// buried under kScreeArid to kScreeHumid of their height.
constexpr double kBandLow = 40.0;
constexpr double kBandHigh = 240.0;
constexpr double kDip = 250.0;
constexpr double kRim = 25.0;
constexpr double kRunArid = 0.04;
constexpr double kRunHumid = 0.15;
constexpr double kHardArid = 0.6;
constexpr double kHardHumid = 0.4;
constexpr double kTilt = 0.15;
constexpr double kSpread = 0.6;
constexpr double kScreeArid = 0.12;
constexpr double kScreeHumid = 0.45;
// Over them, stacks of thicknesses of their own (no comb of equal bands): members kMemberLow to
// kMemberHigh meters (Noises::memberBands), kMemberWeight as strong, kHardMembers of them hard (no
// long soft run); beds kBedLow to kBedHigh meters (Noises::bedBands), kBedWeight as strong,
// kHardBeds of them hard. Their rims recede kMemberRim and kBedRim as far.
constexpr double kMemberLow = 35.0;
constexpr double kMemberHigh = 80.0;
constexpr double kMemberWeight = 0.7;
constexpr double kHardMembers = 0.5;
constexpr double kMemberRim = 0.5;
constexpr double kBedLow = 6.0;
constexpr double kBedHigh = 20.0;
constexpr double kBedWeight = 0.35;
constexpr double kHardBeds = 0.35;
// Where the taluses are soft (Shape::soft), kSoftHard of the hard formations and members stay.
constexpr double kSoftHard = 0.15;
constexpr double kBedRim = 0.15;
// The top of a plateau lies kTopLayer of a layer up its formation, member and bed.
constexpr double kTopLayer = 0.5;
// The forced hard layers of a wall (its caprock, its great cliff): all hard (kUnitHard), their
// treads rising kUnitTilt of a layer, their faces rising over kUnitRun of the height of the wall,
// kUnitScree of them buried; the rims of those of a caprock receding kUnitRim of a layer (its
// ledges, a few meters wide), those of a great cliff not (one face).
constexpr double kUnitHard = 1.1;
constexpr double kUnitTilt = 0.03;
constexpr double kUnitRun = 0.01;
constexpr double kUnitScree = 0.05;
constexpr double kUnitRim = 0.15;
// The layer of the stack of the forced hard layers whose lower bound tops them.
constexpr std::int64_t kTopLayerOfUnit = 8;

// The relief of the tops: kTopFine meters of Noises::topFine, kTopCoarse of Noises::topCoarse
// (Shape::top fades it out at the rims and in the canyons).
constexpr double kTopFine = 14.0;
constexpr double kTopCoarse = 10.0;

double between(double low, double high, const noise::FbmOctaves& f, double x, double z) {
    return low + (high - low) * smooth(-0.5, 0.5, static_cast<double>(f.fbm2D(x, z)));
}

// The forced hard layers of s over base at (x, z): layersAt layers in its height (one exactly for a
// great cliff), the bound of layer kTopLayerOfUnit of their stack exactly at its top (a top never
// cut by a ledge), those under it stepping down to its foot (their ledges at fractions of the
// height that change with their count).
mountain_walls::StrataPass unitPass(const Noises& n, double x, double z, const Shape& s,
                                    double base, double slope) {
    const double whole = mountain_walls::layerBound(n.units, kTopLayerOfUnit) -
                         mountain_walls::layerBound(n.units, kTopLayerOfUnit - 1);
    const double band = s.unitHeight / (s.unitWhole ? whole : layersAt(n.walls, x, z));
    const double top = base + s.unitTop;
    return {slope,
            band,
            top - band * mountain_walls::layerBound(n.units, kTopLayerOfUnit),
            s.unitWhole ? 0.0 : kUnitRim * band,
            kUnitRun * s.unitHeight,
            {n.units, kUnitHard, kUnitTilt, 0.0, kUnitScree},
            s.unitWeight};
}

} // namespace

mountain_walls::WallPasses passesAt(const Noises& n, double x, double z, const TablelandPoint& p,
                                    const Shape& s, double base) {
    mountain_walls::WallPasses out; // the weights of its passes 0
    const double layered = s.rock * (1.0 - s.top) * (1.0 - s.unitCover);
    const double slope = dmath::hypot(s.gradX, s.gradZ);
    if (layered > 0.0) {
        const auto arid = static_cast<double>(p.arid);
        const double band = between(kBandLow, kBandHigh, n.bands, x, z);
        const double member = between(kMemberLow, kMemberHigh, n.memberBands, x, z);
        const double bed = between(kBedLow, kBedHigh, n.bedBands, x, z);
        const double top = base + s.thickness;
        const double dip = kDip * static_cast<double>(n.dip.fbm2D(x, z));
        const double rims = 0.5 + 0.5 * static_cast<double>(n.rims.fbm2D(x, z));
        const double run = mix(kRunHumid, kRunArid, arid) * (2.0 - rims);
        const double scree = mix(kScreeHumid, kScreeArid, arid);
        const double hard = 1.0 - (1.0 - kSoftHard) * s.soft;
        out.passes[0] = {slope,
                         band,
                         top - kTopLayer * band + dip,
                         kRim * rims,
                         run * band,
                         {n.layers, hard * mix(kHardHumid, kHardArid, arid), kTilt, kSpread, scree},
                         layered};
        out.passes[1] = {slope,
                         bed,
                         top - kTopLayer * bed,
                         kBedRim * kRim * rims,
                         run * bed,
                         {n.beds, kHardBeds, kTilt, kSpread, scree},
                         kBedWeight * layered};
        out.passes[2] = {slope,
                         member,
                         top - kTopLayer * member,
                         kMemberRim * kRim * rims,
                         run * member,
                         {n.members, hard * kHardMembers, kTilt, kSpread, scree},
                         kMemberWeight * layered};
    }
    if (s.unitWeight > 0.0 && s.unitHeight > 0.0) {
        out.passes[3] = unitPass(n, x, z, s, base, slope);
    }
    return out;
}

double unitBench(const Noises& n, double x, double z, const Shape& s, double base) {
    const double layers = layersAt(n.walls, x, z);
    const double band = s.unitHeight / layers;
    const double top = base + s.unitTop;
    const double foot = top - s.unitHeight;
    const double offset = top - band * mountain_walls::layerBound(n.units, kTopLayerOfUnit);
    // The bounds under the top, down to the first at the foot or under it (a few: the layers of a
    // caprock are 0.3 to 1.7 of a band each).
    double bench = top;
    for (std::int64_t k = kTopLayerOfUnit - 1; k > kTopLayerOfUnit - 16; --k) {
        const double bound = offset + band * mountain_walls::layerBound(n.units, k);
        if (!(bound > foot)) {
            break;
        }
        bench = bound;
    }
    return bench;
}

double topRelief(const Noises& n, double x, double z, const Shape& s) {
    if (!(s.top > 0.0)) {
        return 0.0;
    }
    return s.top * (kTopFine * static_cast<double>(n.topFine.fbm2D(x, z)) +
                    kTopCoarse * static_cast<double>(n.topCoarse.fbm2D(x, z)));
}

} // namespace terrain::tableland_relief
