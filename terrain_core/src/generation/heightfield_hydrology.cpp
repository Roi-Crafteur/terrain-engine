// The water of the world in a sample of the Heightfield (phase 11B): the valleys of the rivers
// carved into the relief, with the rock of their walls, and the water of the rivers and lakes.
#include "terrain/generation/heightfield.hpp"

#include "heightfield_layers.hpp"
#include "mountain_walls.hpp"
#include "ravines.hpp"
#include "relief_local.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace terrain {

namespace {

// The rock shows on the walls from their foot to kRockShow meters up them.
constexpr double kRockShow = 25.0;
// The rough rock: a ridged noise of kRockPlain meters in the plains to kRockMountain under the
// mountains, centered.
constexpr double kRockPlain = 6.0;
constexpr double kRockMountain = 40.0;
constexpr double kRockSharpness = 2.0;
// The ravines of the relief cut the walls, at most kRavineRoom of their height deep.
constexpr double kRavineRoom = 0.6;
constexpr double kRavineSlope = 1.2; // the slope that orients them, at most
// The walls of the gorges are rock: strata or crystalline, as the map's walls (character
// kCharacter), their ledges at most kStrataRoom of the height of the walls deep.
constexpr float kCharacter = 0.6f;
constexpr double kStrataRoom = 0.4;
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

void Heightfield::applyHydrology(double x, double z, HeightSample& s) const {
    const float relief = s.height;
    const RiverCarve c = hydrology_->riversAt(x, z, static_cast<double>(s.height));
    if (c.carved) {
        double h = c.height;
        // The rock fades out up the walls to where they meet the relief or another valley: no
        // step at the edge of the valley, nor at the crease between two.
        const double rim = std::min(c.second, static_cast<double>(relief));
        const double show =
            smooth(0.0, kRockShow, c.above) * smooth(0.0, kEdgeFade, rim - c.height);
        if (show > 0.0) {
            const double rock = kRockPlain + (kRockMountain - kRockPlain) * c.mountain;
            if (layers_->geographyRock.octaves() > 0) {
                h += show * rock *
                     (static_cast<double>(layers_->geographyRock.ridged2D(x, z, kRockSharpness)) -
                      0.5);
            }
            const double slope = std::min(c.slope, kRavineSlope);
            const float cut = ravines::depth(layers_->ravines, x, z, slope * c.upX,
                                             slope * c.upZ, show);
            h += static_cast<double>(
                relief_local::boundBelow(-cut, static_cast<float>(kRavineRoom * c.above)));
            if (c.gorge > 0.0f) {
                const double share = mountain_walls::strataShare(layers_->walls, x, z, kCharacter,
                                                                 nullptr);
                const double wall = show * static_cast<double>(c.gorge);
                const mountain_walls::Masks masks{wall * share, wall * (1.0 - share), 0.0};
                const double top = mountain_walls::rockTop(layers_->walls, masks, x, z, h,
                                                           c.slope * c.upX, c.slope * c.upZ,
                                                           nullptr);
                h += static_cast<double>(relief_local::boundBelow(
                    static_cast<float>(top - h), static_cast<float>(kStrataRoom * c.above)));
            }
        }
        if (h < static_cast<double>(s.height)) {
            s.height = static_cast<float>(h);
        }
    }
    if (c.wet && s.water != WaterKind::Ocean && s.height < c.level) {
        s.water = WaterKind::River;
        s.waterLevel = c.level;
    }
    if (s.water != WaterKind::Ocean) {
        const LakeWater lake = hydrology_->lakeAt(x, z, static_cast<double>(relief));
        if (lake.lake >= 0 && !(lake.level < s.waterLevel && s.water == WaterKind::River)) {
            s.water = lake.kind;
            s.waterLevel = lake.level;
        }
    }
}

} // namespace terrain
