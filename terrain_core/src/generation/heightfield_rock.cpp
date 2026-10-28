// The rock of the Heightfield in three dimensions (rock_faces.hpp, phase 12A): the samples of
// samplesAt with the faces the walls lay at their points (the walls of the mountains:
// heightfield_geography.cpp, wall_faces.hpp), anchored on the relief at the edges of their runs.
#include "terrain/generation/heightfield.hpp"

#include "heightfield_layers.hpp"

#include <cstddef>
#include <stdexcept>

namespace terrain {

void Heightfield::samplesAt(std::span<const double> xs, std::span<const double> zs,
                            std::span<HeightSample> out, std::span<RockColumn> rock) const {
    if (xs.size() != zs.size() || xs.size() != out.size() || xs.size() != rock.size()) {
        throw std::invalid_argument("Heightfield: the points and the results differ in size");
    }
    // As samplesAt: the relief of every point first, then its water.
    Memos memos;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        rock[i] = RockColumn{};
        out[i] = reliefWith(xs[i], zs[i], &memos, &rock[i]);
        rock[i].uncarved = out[i].height;
        anchorFaces(xs[i], zs[i], memos, rock[i]);
    }
    if (hydrology_) {
        waterOf(xs, zs, out, memos);
    }
    for (std::size_t i = 0; i < xs.size(); ++i) {
        rock[i].ground = out[i].height;
    }
}

// The faces the walls lay at (x, z), their heights relative to the column, anchored on the relief
// at the edges of their runs; the faces that fade out are dropped.
void Heightfield::anchorFaces(double x, double z, Memos& memos, RockColumn& rock) const {
    RockColumn anchored;
    for (std::size_t i = 0; i < rock.faceCount; ++i) {
        RockFace face = rock.faces[i];
        const double half = 0.5 * static_cast<double>(face.run);
        const double toTop = half + face.distance;
        const double toFoot = half - face.distance;
        const float top =
            reliefWith(x - toTop * face.airX, z - toTop * face.airZ, &memos).height;
        const float foot =
            reliefWith(x + toFoot * face.airX, z + toFoot * face.airZ, &memos).height;
        anchorFace(face, foot, top);
        if (face.weight > 0.0f) {
            anchored.add(face);
        }
    }
    anchored.uncarved = rock.uncarved;
    rock = anchored;
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
