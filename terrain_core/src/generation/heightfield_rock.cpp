// The rock of the Heightfield in three dimensions (rock_faces.hpp, phase 12A): the samples of
// samplesAt with the faces the walls lay at their points (the walls of the mountains:
// heightfield_geography.cpp, wall_faces.hpp), anchored on the relief at the edges of their runs.
#include "terrain/generation/heightfield.hpp"

#include "heightfield_layers.hpp"
#include "wall_faces.hpp"

#include <cstddef>
#include <stdexcept>

namespace terrain {

namespace {

// The slope of the ground of the walls at a column is measured kProbe meters away.
constexpr double kProbe = 1.0;

} // namespace

void Heightfield::samplesAt(std::span<const double> xs, std::span<const double> zs,
                            std::span<HeightSample> out, std::span<RockColumn> rock) const {
    if (xs.size() != zs.size() || xs.size() != out.size() || xs.size() != rock.size()) {
        throw std::invalid_argument("Heightfield: the points and the results differ in size");
    }
    // As samplesAt: the relief of every point first, then its water.
    Memos memos;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        rock[i] = RockColumn{};
        wall_faces::Site site;
        site.faces = true;
        out[i] = reliefWith(xs[i], zs[i], &memos, &site);
        rock[i].uncarved = out[i].height;
        placeFaces(xs[i], zs[i], memos, site, rock[i]);
    }
    if (hydrology_) {
        waterOf(xs, zs, out, memos);
    }
    for (std::size_t i = 0; i < xs.size(); ++i) {
        rock[i].ground = out[i].height;
    }
}

void Heightfield::placeFaces(double x, double z, Memos& memos, const wall_faces::Site& site,
                             RockColumn& rock) const {
    if (site.count == 0) {
        return;
    }
    // The slope of the ground of the walls, from kProbe meters away along x and along z (that of
    // the great forms where the walls end there).
    double gradX = site.gradX;
    double gradZ = site.gradZ;
    wall_faces::Site alongX;
    wall_faces::Site alongZ;
    static_cast<void>(reliefWith(x + kProbe, z, &memos, &alongX));
    static_cast<void>(reliefWith(x, z + kProbe, &memos, &alongZ));
    if (alongX.walls && alongZ.walls) {
        gradX = (alongX.ground - site.ground) / kProbe;
        gradZ = (alongZ.ground - site.ground) / kProbe;
    }
    for (std::size_t i = 0; i < site.count; ++i) {
        RockFace face = wall_faces::placeFace(site.pending[i], gradX, gradZ);
        const double half = 0.5 * static_cast<double>(face.run);
        const double toTop = half + face.distance;
        const double toFoot = half - face.distance;
        const float top =
            reliefWith(x - toTop * face.airX, z - toTop * face.airZ, &memos).height;
        const float foot =
            reliefWith(x + toFoot * face.airX, z + toFoot * face.airZ, &memos).height;
        anchorFace(face, foot, top);
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
