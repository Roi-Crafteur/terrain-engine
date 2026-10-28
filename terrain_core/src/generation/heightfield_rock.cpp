// The rock of the Heightfield in three dimensions (rock_faces.hpp, phase 12A): the samples of
// samplesAt with the faces the walls lay at their points (the walls of the mountains:
// heightfield_geography.cpp, wall_faces.hpp).
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
    }
    if (hydrology_) {
        waterOf(xs, zs, out, memos);
    }
    for (std::size_t i = 0; i < xs.size(); ++i) {
        rock[i].ground = out[i].height;
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
