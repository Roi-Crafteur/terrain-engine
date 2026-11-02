// The rock of the Heightfield in three dimensions (rock_faces.hpp, phase 12A): the samples of
// samplesAt with the faces the walls lay at their points (the walls of the mountains:
// heightfield_geography.cpp, wall_faces.hpp), anchored on the relief at the column (their foot
// and top as the walls give them there, over its relief: the relief at the edges of their run
// would follow the next risers along the face).
#include "terrain/generation/heightfield.hpp"

#include "coast_faces.hpp"
#include "heightfield_layers.hpp"
#include "terrain/generation/noise.hpp"
#include "wall_faces.hpp"
#include "water/slot_faces.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terrain {

namespace {

// The slope of the ground of the walls at a column is measured kProbe meters away.
constexpr double kProbe = 1.0;
// A wall of a slot shapes a column whose ground lies within kWallGround meters of its heights.
constexpr float kWallGround = 1.0f;

// The points of a batch kProbe meters along x and along z from each point, when the batch holds
// them where a lattice of rows of a constant step would (the rows of a tile): the next point, and
// the point one row on.
class Neighbors {
public:
    Neighbors(std::span<const double> xs, std::span<const double> zs) : xs_(xs), zs_(zs) {
        while (row_ < zs.size() && zs[row_] == zs[0]) {
            ++row_;
        }
    }
    [[nodiscard]] std::size_t alongX(std::size_t i) const {
        return holds(i + 1, xs_[i] + kProbe, zs_[i]) ? i + 1 : kNone;
    }
    [[nodiscard]] std::size_t alongZ(std::size_t i) const {
        return holds(i + row_, xs_[i], zs_[i] + kProbe) ? i + row_ : kNone;
    }

private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    [[nodiscard]] bool holds(std::size_t j, double x, double z) const {
        return j < xs_.size() && xs_[j] == x && zs_[j] == z;
    }
    std::span<const double> xs_;
    std::span<const double> zs_;
    std::size_t row_ = 0;
};

// Places the faces the walls of a gorge laid at a column (Heightfield::gorgeFaces) along the
// slope of its valley and anchors them on its ground, as the faces of the mountains (placeFaces):
// carved faces, of the valley.
void placeGorgeFaces(const wall_faces::Site& site, float leastRun, RockColumn& rock) {
    for (std::size_t i = 0; i < site.count; ++i) {
        RockFace face = wall_faces::placeFace(site.pending[i], site.gradX, site.gradZ);
        if (!(face.run >= leastRun)) {
            continue;
        }
        const float ground = rock.ground;
        anchorFace(face, ground + face.foot, std::max(ground + face.foot, ground + face.top));
        face.carved = true;
        if (face.weight > 0.0f) {
            rock.add(face);
        }
    }
}

} // namespace

void Heightfield::samplesAt(std::span<const double> xs, std::span<const double> zs,
                            std::span<HeightSample> out, std::span<RockColumn> rock,
                            float leastRun) const {
    if (xs.size() != zs.size() || xs.size() != out.size() || xs.size() != rock.size()) {
        throw std::invalid_argument("Heightfield: the points and the results differ in size");
    }
    // As samplesAt: the relief of every point first, then its water; the faces once every point
    // reported the ground of its walls, the point 1 m along x and along z serving as probes when
    // the batch holds it (the same value as a probe).
    Memos memos;
    std::vector<wall_faces::Site> grounds(xs.size()); // the ground of the walls alone
    std::vector<std::pair<std::size_t, wall_faces::Site>> faced;
    // The faces of the coasts (agent 33, coast_faces.hpp), relative to the ground of the coast.
    std::vector<std::pair<std::size_t, RockFace>> coastal;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        wall_faces::Site site;
        site.faces = true;
        coast_faces::Report report;
        memos.coastFace = &report;
        out[i] = reliefWith(xs[i], zs[i], &memos, &site);
        memos.coastFace = nullptr;
        grounds[i].walls = site.walls;
        grounds[i].ground = site.ground;
        if (site.count > 0) {
            faced.emplace_back(i, site);
        }
        if (report.faced) {
            coastal.emplace_back(i, report.face);
        }
    }
    const Neighbors neighbors(xs, zs);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        rock[i] = RockColumn{};
        rock[i].uncarved = out[i].height;
    }
    for (const auto& [i, site] : faced) {
        const std::size_t alongX = neighbors.alongX(i);
        const std::size_t alongZ = neighbors.alongZ(i);
        placeFaces(xs[i], zs[i], memos, site, alongX < xs.size() ? &grounds[alongX] : nullptr,
                   alongZ < xs.size() ? &grounds[alongZ] : nullptr, out[i].height, leastRun,
                   rock[i]);
    }
    for (auto& [i, face] : coastal) {
        anchorCoastFace(noise::wrapCanonical(xs[i], world_.x), noise::wrapCanonical(zs[i], world_.z),
                        out[i].height, face);
        if (face.run >= leastRun && face.weight > 0.0f) {
            rock[i].add(face);
        }
    }
    // The walls of the slots only when their faces are drawn (slot_faces.hpp: kSlotFacesDrawn).
    std::vector<SlotWall> walls;
    std::vector<std::pair<std::size_t, wall_faces::Site>> gorges;
    if (hydrology_) {
        if constexpr (water::kSlotFacesDrawn) {
            walls.resize(xs.size());
        }
        const WaterRock outputs{walls, &gorges};
        waterOf(xs, zs, out, memos, &outputs);
    }
    for (std::size_t i = 0; i < xs.size(); ++i) {
        rock[i].ground = out[i].height;
    }
    if (!walls.empty()) {
        placeSlotFaces(xs, zs, walls, leastRun, rock);
    }
    for (const auto& [i, site] : gorges) {
        placeGorgeFaces(site, leastRun, rock[i]);
    }
}

void Heightfield::placeSlotFaces(std::span<const double> xs, std::span<const double> zs,
                                 std::span<const SlotWall> walls, float leastRun,
                                 std::span<RockColumn> rock) const {
    for (std::size_t i = 0; i < walls.size(); ++i) {
        const SlotWall& w = walls[i];
        // Only where the ground of the column is that of the wall (within kWallGround meters of
        // its foot and top).
        const float ground = rock[i].ground;
        if (!(w.width > 0.0f) || !(w.weight > 0.0f) || !(w.width >= leastRun) ||
            !(ground >= std::min(w.foot, w.top) - kWallGround) ||
            !(ground <= std::max(w.foot, w.top) + kWallGround)) {
            continue;
        }
        const double x = noise::wrapCanonical(xs[i], world_.x);
        const double z = noise::wrapCanonical(zs[i], world_.z);
        const float hardness =
            water::notched(w) ? rockHardness(x, z, water::notchHardnessHeight(w)) : 0.0f;
        const RockFace face = water::slotFace(w, x, z, hardness, world_);
        if (face.weight > 0.0f) {
            rock[i].add(face);
        }
    }
}

void Heightfield::placeFaces(double x, double z, Memos& memos, const wall_faces::Site& site,
                             const wall_faces::Site* alongX, const wall_faces::Site* alongZ,
                             float relief, float leastRun, RockColumn& rock) const {
    // The slope of the ground of the walls, from kProbe meters away along x and along z (that of
    // the great forms where the walls end there).
    wall_faces::Site probeX;
    wall_faces::Site probeZ;
    if (alongX == nullptr) {
        static_cast<void>(reliefWith(x + kProbe, z, &memos, &probeX));
        alongX = &probeX;
    }
    if (alongZ == nullptr) {
        static_cast<void>(reliefWith(x, z + kProbe, &memos, &probeZ));
        alongZ = &probeZ;
    }
    double gradX = site.gradX;
    double gradZ = site.gradZ;
    if (alongX->walls && alongZ->walls) {
        gradX = (alongX->ground - site.ground) / kProbe;
        gradZ = (alongZ->ground - site.ground) / kProbe;
    }
    for (std::size_t i = 0; i < site.count; ++i) {
        RockFace face = wall_faces::placeFace(site.pending[i], gradX, gradZ);
        if (!(face.run >= leastRun)) {
            continue;
        }
        anchorFace(face, relief + face.foot, std::max(relief + face.foot, relief + face.top));
        if (face.weight > 0.0f) {
            rock.add(face);
        }
    }
}

RockColumn Heightfield::rockAt(double x, double z) const {
    const double xs[] = {x};
    const double zs[] = {z};
    HeightSample sample;
    RockColumn rock;
    samplesAt(xs, zs, std::span<HeightSample>(&sample, 1), std::span<RockColumn>(&rock, 1));
    return rock;
}

} // namespace terrain
