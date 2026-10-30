// The water of the world in a sample of the Heightfield (phase 11B): the valleys of the rivers
// carved into the relief, with the rock of their walls, and the water of the rivers and lakes.
#include "terrain/generation/heightfield.hpp"

#include "heightfield_layers.hpp"
#include "mountain_walls.hpp"
#include "ravines.hpp"
#include "relief_local.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <utility>

namespace terrain {

namespace {

// The rock shows on the walls from their foot to kRockShow meters up them.
constexpr double kRockShow = 25.0;
// The rough rock: a ridged noise of kRockPlain meters in the plains to kRockMountain under the
// mountains, centered, plus kGorgeRock of the height of the walls of a gorge (as rough as they
// are high, as the walls of the massifs: no smooth face).
constexpr double kRockPlain = 6.0;
constexpr double kRockMountain = 40.0;
constexpr double kGorgeRock = 0.3;
constexpr double kRockSharpness = 2.0;
// The ravines of the relief cut the walls, and the couloirs (layers gullies) those of the
// gorges, at most kRavineRoom of their height deep.
constexpr double kRavineRoom = 0.6;
constexpr double kRavineSlope = 1.2; // the slope that orients them, at most
constexpr double kGullySlope = 2.0;  // and the couloirs of the walls of the gorges
// The grain of the rock of the walls of the gorges (layers wallGrain): kGrainShare of their
// height, kGrainMost meters at most, their faces broken at the scale of the rock steps of the
// walls of the massifs (phase 11R bis).
constexpr double kGrainShare = 0.1;
constexpr double kGrainMost = 15.0;
// The walls of the gorges are rock: strata or crystalline, as the map's walls (character
// kCharacter), their ledges at most kStrataRoom of the height of the walls deep.
constexpr float kCharacter = 0.6f;
constexpr double kStrataRoom = 0.4;
constexpr double kLedge = 1.0;
// The levees fill the floodplain up to kLevee meters (river_course.hpp); the water of a channel
// stands at most kAboveGround meters above the ground beside it.
constexpr double kLevee = 5.0;
constexpr double kAboveGround = 2.0;
constexpr double kFloorRise = 0.6; // the floor of a valley above its water (river_profile.cpp)
// The rock fades out over the last kEdgeFade meters below the relief, where the walls meet it,
// and below the valley of another river, where two valleys meet (the crease between them): a
// valley that comes to carve the relief starts at its height, the rock never jumps.
constexpr double kEdgeFade = 40.0;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

Heightfield Heightfield::withHydrology(std::shared_ptr<const Hydrology> hydrology) const {
    Heightfield out = *this;
    out.hydrology_ = std::move(hydrology);
    return out;
}

// The rock of the walls of a valley over its surface h, show of it showing: rough, cut by the
// ravines of the relief, in a gorge grained, cut by couloirs and laid in strata or crystalline.
double Heightfield::valleyRockAt(double x, double z, double h, const RiverCarve& c, double show,
                                 Memos* memos) const {
    const double rock = kRockPlain + (kRockMountain - kRockPlain) * c.mountain +
                        kGorgeRock * c.gorge * c.above;
    if (layers_->geographyRock.octaves() > 0) {
        const float ridged =
            memos != nullptr ? layers_->geographyRock.ridged2D(x, z, kRockSharpness, memos->rock)
                             : layers_->geographyRock.ridged2D(x, z, kRockSharpness);
        h += show * rock * (static_cast<double>(ridged) - 0.5);
    }
    const double slope = std::min(c.slope, kRavineSlope);
    float cut = ravines::depth(layers_->ravines, x, z, slope * c.upX, slope * c.upZ, show,
                               memos != nullptr ? &memos->ravines : nullptr);
    if (c.gorge > 0.0f && layers_->wallGrain.octaves() > 0) {
        const float grain = memos != nullptr ? layers_->wallGrain.fbm2D(x, z, memos->grain)
                                             : layers_->wallGrain.fbm2D(x, z);
        h += show * static_cast<double>(c.gorge) * std::min(kGrainShare * c.above, kGrainMost) *
             static_cast<double>(grain);
    }
    if (c.gorge > 0.0f) {
        // The couloirs of the walls of the gorges, down their slope.
        const double steep = std::min(c.slope, kGullySlope);
        cut += ravines::depth(layers_->gullies, x, z, steep * c.upX, steep * c.upZ,
                              show * static_cast<double>(c.gorge),
                              memos != nullptr ? &memos->gullies : nullptr);
    }
    h += static_cast<double>(
        relief_local::boundBelow(-cut, static_cast<float>(kRavineRoom * c.above)));
    if (c.gorge > 0.0f) {
        mountain_walls::Memo* walls = memos != nullptr ? &memos->walls : nullptr;
        const double share = mountain_walls::strataShare(layers_->walls, x, z, kCharacter, walls);
        const double wall = show * static_cast<double>(c.gorge);
        const mountain_walls::Masks masks{wall * share, wall * (1.0 - share), 0.0};
        const double top = mountain_walls::rockTop(layers_->walls, masks, x, z, h,
                                                   kLedge * c.upX, kLedge * c.upZ, walls);
        h += static_cast<double>(relief_local::boundBelow(
            static_cast<float>(top - h), static_cast<float>(kStrataRoom * c.above)));
    }
    return h;
}

void Heightfield::applyHydrology(double x, double z, HeightSample& s, Memos* memos) const {
    const float relief = s.height;
    Hydrology::Region* region = memos != nullptr ? &memos->water : nullptr;
    const RiverCarve c = hydrology_->riversAt(x, z, static_cast<double>(s.height), region);
    // The levees: the floodplain filled up to the floor of the valley.
    const double filled = static_cast<double>(relief) + c.raise;
    s.height = static_cast<float>(filled);
    if (c.carved) {
        double h = c.height;
        // The rock fades out up the walls to where they meet the relief or another valley: no
        // step at the edge of the valley, nor at the crease between two.
        const double rim = std::min(c.second, filled);
        const double show =
            smooth(0.0, kRockShow, c.above) * smooth(0.0, kEdgeFade, rim - c.height);
        if (show > 0.0) {
            h = valleyRockAt(x, z, h, c, show, memos);
        }
        // The rock never takes the walls below the floor beside the water (beside a fall, the
        // floor of its valley on the slope), nor below the banks.
        h = std::max(h, std::min(c.height,
                                 std::max(static_cast<double>(c.floor) + kFloorRise, c.bank)));
        if (h < static_cast<double>(s.height)) {
            s.height = static_cast<float>(h);
        }
    }
    // The water of the channel, never more than kAboveGround meters over the ground beside it:
    // where the relief lies deeper below it than the levees fill, the bed stays dry.
    if (c.wet && s.water != WaterKind::Ocean && s.height < c.level &&
        !(static_cast<double>(relief) < static_cast<double>(c.level) - kLevee - kAboveGround)) {
        s.water = WaterKind::River;
        s.waterLevel = c.level;
    }
    // The ponds of the rivers, where the relief holds their water.
    if (s.water != WaterKind::Ocean && s.height < c.pond &&
        !(s.water == WaterKind::River && !(s.waterLevel < c.pond))) {
        s.water = WaterKind::River;
        s.waterLevel = c.pond;
    }
    if (s.water != WaterKind::Ocean) {
        const LakeWater lake = hydrology_->lakeAt(x, z, static_cast<double>(relief), region);
        if (lake.lake >= 0 && !(lake.level < s.waterLevel && s.water == WaterKind::River)) {
            s.water = lake.kind;
            s.waterLevel = lake.level;
        }
    }
}

void Heightfield::waterOf(std::span<const double> xs, std::span<const double> zs,
                          std::span<HeightSample> samples, Memos& memos) const {
    double minX = std::numeric_limits<double>::infinity();
    double minZ = minX;
    double maxX = -minX;
    double maxZ = -minX;
    double groundMin = minX;
    double groundMax = -minX;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const double x = noise::wrapCanonical(xs[i], world_.x);
        const double z = noise::wrapCanonical(zs[i], world_.z);
        leaveValleys(x, z, samples[i], &memos);
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minZ = std::min(minZ, z);
        maxZ = std::max(maxZ, z);
        groundMin = std::min(groundMin, static_cast<double>(samples[i].height));
        groundMax = std::max(groundMax, static_cast<double>(samples[i].height));
    }
    if (!xs.empty()) {
        memos.water = hydrology_->regionOf(minX, minZ, maxX, maxZ, groundMin, groundMax);
    }
    for (std::size_t i = 0; i < xs.size(); ++i) {
        applyHydrology(noise::wrapCanonical(xs[i], world_.x), noise::wrapCanonical(zs[i], world_.z),
                       samples[i], &memos);
    }
}

} // namespace terrain
