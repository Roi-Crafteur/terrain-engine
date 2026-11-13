// The rock of the plateaus (agent 46, tableland_relief.hpp: passesAt, topRelief): the strata of
// the walls of the mountains (mountain_walls.hpp) laid parallel to the tops of the plateaus.
#include "tableland_relief.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>

namespace terrain::tableland_relief {

namespace {

// The rock: formations kBandLow to kBandHigh meters thick (Noises::bands), offset by up to kDip
// (Noises::dip: a gentle regional dip), their rims receded by up to kRim (Noises::rims), their
// faces rising over kRunArid to kRunHumid of a band (up to twice that
// where the rims recede least); kHardArid to kHardHumid of them hard, their treads rising kTilt
// of their band plus up to kSpread of the rest, the foot of their cliffs buried under kScreeArid to
// kScreeHumid of their height. Over them beds kBedShare of a band, kBedWeight as strong, kHardBeds
// of them hard.
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
constexpr double kBedShare = 0.15;
constexpr double kBedWeight = 0.35;
constexpr double kHardBeds = 0.35;
constexpr double kBedOffset = 0.37;
// A second stack of members kMemberShare of a band thick, kMemberWeight as strong, kHardMembers of
// them hard: where a run of soft formations would leave a long smooth slope, its cliffs stand.
constexpr double kMemberShare = 0.55;
constexpr double kMemberWeight = 0.7;
constexpr double kHardMembers = 0.5;
// The top of a plateau lies kTopLayer of a band up its formation (and of a bed up its bed).
constexpr double kTopLayer = 0.5;

// The relief of the tops: kTopFine meters of Noises::topFine, kTopCoarse of Noises::topCoarse
// (Shape::top fades it out at the rims and in the canyons).
constexpr double kTopFine = 14.0;
constexpr double kTopCoarse = 10.0;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double mix(double humid, double arid, double share) {
    return humid + (arid - humid) * share;
}

} // namespace

mountain_walls::WallPasses passesAt(const Noises& n, double x, double z, const TablelandPoint& p,
                                    const Shape& s, double regional) {
    mountain_walls::WallPasses out; // the weights of its passes 0
    if (!(s.rock > 0.0)) {
        return out;
    }
    const auto arid = static_cast<double>(p.arid);
    const double band =
        kBandLow + (kBandHigh - kBandLow) *
                       smooth(-0.5, 0.5, static_cast<double>(n.bands.fbm2D(x, z)));
    // The top in the middle of a formation (and of a bed), whatever the dip.
    const double top = regional + s.thickness - kTopLayer * band;
    const double dip = kDip * static_cast<double>(n.dip.fbm2D(x, z));
    const double offset = top + dip;
    const double rims = 0.5 + 0.5 * static_cast<double>(n.rims.fbm2D(x, z));
    const double run = mix(kRunHumid, kRunArid, arid) * (2.0 - rims) * band;
    const double slope = dmath::hypot(s.gradX, s.gradZ);
    const double scree = mix(kScreeHumid, kScreeArid, arid);
    out.passes[0] = {slope,  band,
                     offset, kRim * rims,
                     run,    {n.layers, mix(kHardHumid, kHardArid, arid), kTilt, kSpread, scree},
                     s.rock};
    out.passes[1] = {slope,
                     kBedShare * band,
                     top + kTopLayer * band * (1.0 - kBedShare) + kBedOffset * dip,
                     kBedShare * kRim * rims,
                     kBedShare * run,
                     {n.beds, kHardBeds, kTilt, kSpread, scree},
                     kBedWeight * s.rock};
    const double member = kMemberShare * band;
    out.passes[2] = {slope,
                     member,
                     top + kTopLayer * (band - member) - dip,
                     kMemberShare * kRim * rims,
                     kMemberShare * run,
                     {n.members, kHardMembers, kTilt, kSpread, scree},
                     kMemberWeight * s.rock};
    return out;
}

double topRelief(const Noises& n, double x, double z, const Shape& s) {
    if (!(s.top > 0.0)) {
        return 0.0;
    }
    return s.top * (kTopFine * static_cast<double>(n.topFine.fbm2D(x, z)) +
                    kTopCoarse * static_cast<double>(n.topCoarse.fbm2D(x, z)));
}

} // namespace terrain::tableland_relief
