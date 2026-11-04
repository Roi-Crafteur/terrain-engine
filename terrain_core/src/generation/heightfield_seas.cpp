// The seas over the floor of the sea (agent 51, seas.hpp): after the dunes and before the rivers
// and lakes, which never take the sea.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/seas.hpp"
#include "terrain/generation/water/hydrology.hpp"

namespace terrain {

void Heightfield::addSeas(double x, double z, HeightSample& s) const {
    const Seas* seas = hydrology_->seas();
    if (seas == nullptr || s.water != WaterKind::Ocean) {
        return;
    }
    seas->apply(x, z, s, footprint_);
}

} // namespace terrain
