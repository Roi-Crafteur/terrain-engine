#pragma once

// The zone of the volcanoes (agent 45): the discs of their reach (VolcanoSite::reach), out of
// which the world keeps every bit (volcano_outside_test). The tests that pin the world elsewhere
// take their points out of it, as out of the band of the coasts (coast_zone_support.hpp).

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"
#include "terrain/world_size.hpp"

#include <cmath>
#include <cstdint>

namespace volcano_zone {

// Whether a volcano of field reaches (x, z), any image of the point; never without them.
[[nodiscard]] inline bool inZone(const terrain::Heightfield& field, double x, double z) {
    const terrain::Volcanoes* v = field.volcanoes();
    if (v == nullptr) {
        return false;
    }
    const terrain::WorldSize& w = field.world();
    const double cx = terrain::wrapCoordinate(x, w.x);
    const double cz = terrain::wrapCoordinate(z, w.z);
    for (const std::uint32_t i : v->near(cx, cz)) {
        const terrain::VolcanoSite& s = v->sites()[i];
        const terrain::VolcanoOffset o = terrain::volcanoOffset(w, s.x, s.z, cx, cz);
        if (std::hypot(o.dx, o.dz) < static_cast<double>(s.reach)) {
            return true;
        }
    }
    return false;
}

} // namespace volcano_zone
