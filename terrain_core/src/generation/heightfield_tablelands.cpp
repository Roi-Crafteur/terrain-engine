// The plateaus and the karst in the Heightfield (agent 46, tablelands.hpp, tableland_forms.hpp,
// tableland_relief.hpp): what their provinces make of the relief. Off (GeographyParams::tablelands
// and ::karst), the relief of before them, bit for bit; on, the same bits out of their provinces.
#include "terrain/generation/heightfield.hpp"

#include "heightfield_layers.hpp"
#include "tableland_forms.hpp"
#include "tableland_relief.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/tablelands.hpp"
#include "wall_faces.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

float Heightfield::intensityAt(double x, double z, const GeographySample& g, Memos* memos) const {
    const float intensity = reliefAt(x, z, g.relief, memos);
    if (!tablelands_ || !params_.geography.tablelands) {
        return intensity;
    }
    const float surface = tableland_forms::surfaceShare(g);
    if (!(surface > 0.0f)) {
        return intensity;
    }
    return std::max(intensity,
                    tableland_forms::innerRanges(layers_->tablelandForms, x, z, surface));
}

void Heightfield::addSurfaceRelief(double x, double z, const GeographySample& g,
                                   float& local) const {
    if (!tablelands_ || !params_.geography.tablelands) {
        return;
    }
    const float surface = tableland_forms::surfaceShare(g);
    if (surface > 0.0f) {
        local += tableland_forms::surfaceRelief(layers_->tablelandForms, x, z, surface);
    }
}

TablelandPoint Heightfield::provincesAt(double x, double z, const GeographySample& g) const {
    if (!tablelands_ || !params_.geography.tablelands) {
        return {};
    }
    return tablelands_->at(x, z, g);
}

void Heightfield::addTablelands(double x, double z, const TablelandPoint& p, double coast,
                                HeightSample& s, wall_faces::Site* site) const {
    const tableland_relief::Noises& n = layers_->tablelandRelief;
    // The shares around the point, with its coast (it changes little over a few hundred meters).
    const double along = tableland_relief::kShareStep;
    const tableland_relief::Province q{
        p, tablelands_->tablelandShare(noise::wrapCanonical(x + along, world_.x), z, coast),
        tablelands_->tablelandShare(x, noise::wrapCanonical(z + along, world_.z), coast)};
    const tableland_relief::Shape shape = tableland_relief::shapeAt(n, x, z, q);
    if (!(shape.rise > 0.0) && !(shape.rock > 0.0)) {
        return;
    }
    // The ground of the plateau, its rock laid over it (the walls of the plateau), the relief of
    // its tops over that.
    const double ground = static_cast<double>(s.base) + shape.rise;
    const mountain_walls::WallPasses passes =
        tableland_relief::passesAt(n, x, z, p, shape, static_cast<double>(s.regional));
    if (site != nullptr && shape.rock > 0.0) {
        site->walls = true;
        site->ground = ground;
        site->gradX = shape.gradX;
        site->gradZ = shape.gradZ;
        if (site->faces) {
            mountain_walls::Column column;
            column.ground = ground;
            column.gradX = shape.gradX;
            column.gradZ = shape.gradZ;
            wall_faces::addFaces(passes, layers_->faceGrain, shape.rock, column, x, z, *site);
        }
    }
    s.base = static_cast<float>(wall_faces::passesTop(passes, ground) +
                                tableland_relief::topRelief(n, x, z, shape));
    s.tableland = static_cast<std::uint8_t>(std::lround(255.0 * std::clamp(shape.rock, 0.0, 1.0)));
}

} // namespace terrain
