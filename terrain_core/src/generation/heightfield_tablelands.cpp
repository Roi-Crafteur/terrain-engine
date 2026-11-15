// The plateaus and the karst in the Heightfield (agent 46, tablelands.hpp, tableland_forms.hpp,
// tableland_relief.hpp): what their provinces make of the relief. Off (GeographyParams::tablelands
// and ::karst), the relief of before them, bit for bit; on, the same bits out of their provinces.
#include "terrain/generation/heightfield.hpp"

#include "heightfield_layers.hpp"
#include "tableland_forms.hpp"
#include "tableland_relief.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/tablelands.hpp"
#include "wall_faces.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// The faces of the plateaus run kFaceRun meters at least: the tiles of LOD 0 draw them whole
// (rock_faces.hpp: kRunCells of their cells and half as many again), however steep the climb of
// their riser (their columns within half of it on the treads by it).
constexpr double kFaceRun = 6.0;
// The climb of the lowest layer of a caprock holds its rock up (and stands as a face) where the
// ground climbs at kClimbSlope at least (a wall; none under half of that: its talus, a gentle
// ground or a block of scree grazing the foot of a caprock keep the rock of their strata, never a
// mound of the caprock).
constexpr double kClimbSlope = 3.0;
// The rock of a column never stands more than kOverGround meters over the ground of the shape (the
// relief of the tops added over it): a column left standing over its ground is a blade; only the
// cuts under the ground are the rock's own (the risers of hard layers rise a fraction of a meter
// over it).
constexpr double kOverGround = 1.0;

// The lowest layer of the forced hard layers of a caprock over its foot (unitBench), partial: the
// rock keeps to the shape there (never a ledge laid under the foot, a bench eating the batter of
// the cliff), its face the climb from the foot to that bound, down to the rock the strata leave
// under the foot (their tread there: one face). The columns on it and those under its foot within
// its run (kFaceRun at least, about the middle of the climb) see that face (its climb, the top of
// the walls at the column unset), as far as the ground climbs as a wall there (steep, kClimbSlope).
struct Partial {
    bool on = false;
    bool seen = false;
    double steep = 0.0;
    wall_faces::Climb climb;
};
Partial partialOf(const tableland_relief::Noises& n, double x, double z,
                  const tableland_relief::Shape& shape, const mountain_walls::WallPasses& passes,
                  double base, double ground) {
    Partial out;
    const double foot = base + shape.unitTop - shape.unitHeight;
    const double slope = std::max(dmath::hypot(shape.gradX, shape.gradZ), 0.05);
    if (!(shape.unitHeight > 0.0) || shape.unitWhole || !(ground > foot - kFaceRun * slope)) {
        return out;
    }
    const double bench = tableland_relief::unitBench(n, x, z, shape, base);
    const double under = 0.5 * std::max(0.0, kFaceRun * slope - (bench - foot));
    if (!(ground < bench) || !(bench > foot) || !(ground > foot - under)) {
        return out;
    }
    mountain_walls::WallPasses strata = passes;
    strata.passes[3].weight = 0.0;
    out.on = ground > foot && shape.unitWeight > 0.0;
    out.seen = true;
    out.steep = noise::smoothstep(static_cast<float>(0.5 * kClimbSlope),
                                  static_cast<float>(kClimbSlope), static_cast<float>(slope));
    out.climb = {foot, bench, std::min(foot, wall_faces::passesTop(strata, foot)), 0.0};
    return out;
}

// A caprock a canyon lowers with the escarpment (Shape::lowered) never stands over the rock the strata
// leave on the floor of that canyon behind its rim: laid flat there, as much of a top as that floor is
// (no lip of the rim over a floor cut down to the tread of its layer).
double floorRock(const tableland_relief::Noises& n, double x, double z, const TablelandPoint& p,
                 const tableland_relief::Shape& shape, double base) {
    tableland_relief::Shape floor = shape;
    floor.gradX = 0.0;
    floor.gradZ = 0.0;
    floor.top = shape.lowered.top;
    floor.unitCover = 0.0;
    floor.unitWeight = 0.0;
    return wall_faces::passesTop(tableland_relief::passesAt(n, x, z, p, floor, base),
                                 base + shape.lowered.rise);
}

} // namespace

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

void Heightfield::addTablelands(double x, double z, const TablelandPoint& p, HeightSample& s,
                                wall_faces::Site* site) const {
    const tableland_relief::Noises& n = layers_->tablelandRelief;
    // The shares around the point, each with its own coast (the fade from the coast is part of the
    // slope of the share the escarpment follows).
    const double along = tableland_relief::kShareStep;
    const double ax = noise::wrapCanonical(x + along, world_.x);
    const double az = noise::wrapCanonical(z + along, world_.z);
    const tableland_relief::Province q{
        p, tablelands_->tablelandShare(ax, z, geography_->sampleAt(ax, z).coast),
        tablelands_->tablelandShare(x, az, geography_->sampleAt(x, az).coast)};
    const tableland_relief::Shape shape = tableland_relief::shapeAt(n, x, z, q);
    if (!(shape.rise > 0.0) && !(shape.rock > 0.0) && !(shape.unitWeight > 0.0)) {
        return;
    }
    // The ground of the plateau, its rock laid over it (the walls of the plateau), the relief of
    // its tops over that.
    const double ground = static_cast<double>(s.base) + shape.rise;
    const mountain_walls::WallPasses passes =
        tableland_relief::passesAt(n, x, z, p, shape, static_cast<double>(s.base));
    const double weight = std::max(shape.rock, shape.unitWeight);
    double rock = wall_faces::passesTop(passes, ground);
    const double foot = static_cast<double>(s.base) + shape.unitTop - shape.unitHeight;
    Partial partial = partialOf(n, x, z, shape, passes, static_cast<double>(s.base), ground);
    if (partial.on) {
        rock += shape.unitWeight * partial.steep * (ground - rock);
    } else if (shape.unitWeight > 0.0 && !shape.unitWhole && ground > foot) {
        rock = std::max(rock, tableland_relief::unitBench(n, x, z, shape,
                                                          static_cast<double>(s.base)));
    }
    // Over the great cliff of a canyon the soft layers of its upper walls recede down to its top,
    // never into it (the hard layer is the floor of their slopes: no column of it left standing
    // alone over them, a blade). Where it holds the rock up, the faces of the layers it covers are
    // buried.
    const double floor = static_cast<double>(s.base) + shape.floorTop;
    const bool floored = shape.floorTop > 0.0 && ground > floor && rock < floor;
    if (floored) {
        rock = floor;
    }
    bool shaved = false;
    if (shape.lowered.rise > 0.0) {
        const double under = floorRock(n, x, z, p, shape, static_cast<double>(s.base));
        shaved = rock > under;
        rock = std::min(rock, under);
    }
    rock = std::min(rock, ground + kOverGround);
    if (site != nullptr && weight > 0.0) {
        site->walls = true;
        site->ground = ground;
        site->gradX = shape.gradX;
        site->gradZ = shape.gradZ;
        if (site->faces && !floored && !shaved) {
            mountain_walls::Column column;
            column.ground = ground;
            column.gradX = shape.gradX;
            column.gradZ = shape.gradZ;
            const double joints = tableland_relief::jointsAt(n.walls, x, z);
            if (partial.seen && partial.steep > 0.0) {
                partial.climb.top = rock;
                wall_faces::addStepFace(passes, 3, layers_->faceGrain, weight * partial.steep,
                                        column, x, z, partial.climb, *site, joints, kFaceRun);
            }
            if (!partial.on || !(partial.steep > 0.0)) {
                wall_faces::addFaces(passes, layers_->faceGrain, weight, column, x, z, *site,
                                     joints, kFaceRun);
            }
        }
    }
    s.base = static_cast<float>(rock + tableland_relief::topRelief(n, x, z, shape));
    s.tableland = static_cast<std::uint8_t>(std::lround(255.0 * std::clamp(weight, 0.0, 1.0)));
}

} // namespace terrain
