// The volcanoes over the relief (agent 45, volcanoes.hpp): after the sea of the geography and
// before the planet level, so that the planet level, its rivers and its lakes find them.
#include "terrain/generation/heightfield.hpp"

#include "heightfield_layers.hpp"
#include "volcano_forms.hpp"
#include "volcano_relief.hpp"
#include "wall_faces.hpp"
#include "coast_faces.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

namespace terrain {

struct Heightfield::Volcanic {
    explicit Volcanic(std::size_t count) : details(count) {}
    volcano_relief::Details details;
};

std::shared_ptr<const Heightfield::Volcanic> Heightfield::makeVolcanic(const Volcanoes& volcanoes) {
    return std::make_shared<const Volcanic>(volcanoes.sites().size());
}

namespace {

// The faces of the walls and of the coasts the volcanoes bury fade out over the first half of
// their cover: no wall standing on a cone, no cut at the edge of their reach.
void buryFaces(float cover, wall_faces::Site* site, coast_faces::Report* coast) {
    const auto keep = static_cast<float>(1.0 - volcano_forms::smoothstep(0.0, 0.5, cover));
    if (site != nullptr) {
        for (std::size_t i = 0; i < site->count; ++i) {
            site->pending[i].face.weight *= keep;
        }
    }
    if (coast != nullptr) {
        coast->face.weight *= keep;
        if (cover > 0.5f) {
            coast->spire = RockSpire{};
            coast->shore = -1.0f;
        }
    }
}

} // namespace

void Heightfield::addVolcanoes(double x, double z, HeightSample& s, Memos* memos,
                               wall_faces::Site* site) const {
    if (!volcanoes_ || (memos != nullptr && memos->noVolcanoes) || !volcanoes_->mayHold(x, z)) {
        return;
    }
    const volcano_relief::GroundAt ground = [this](double gx, double gz) {
        Memos bare;
        bare.bare = true;
        bare.noVolcanoes = true;
        const HeightSample g = reliefWith(gx, gz, &bare);
        return volcano_relief::Ground{static_cast<double>(g.height),
                                      static_cast<double>(g.regional)};
    };
    const std::vector<VolcanoSite>& sites = volcanoes_->sites();
    volcano_relief::Point p;
    p.height = static_cast<double>(s.height);
    p.regional = static_cast<double>(s.regional);
    for (const std::uint32_t i : volcanoes_->near(x, z)) {
        const VolcanoSite& v = sites[i];
        const VolcanoOffset o = volcanoOffset(world_, v.x, v.z, x, z);
        const auto reach = static_cast<double>(v.reach);
        if (!(o.dx * o.dx + o.dz * o.dz < reach * reach)) {
            continue;
        }
        const volcano_relief::Detail& d = volcanic_->details.get(i, v, ground);
        volcano_relief::apply(v, d, o.dx, o.dz, footprint_, p);
    }
    if (!p.touched) {
        return;
    }
    const auto height = static_cast<float>(p.height);
    s.base += height - s.height;
    s.height = height;
    s.volcanic = static_cast<std::uint8_t>(std::lround(std::clamp(p.cover, 0.0f, 1.0f) * 255.0f));
    // The sea where the geography puts it, but over the land the volcanoes raise (their islands)
    // and in their craters closed above it: a ground below 0 m is the sea only when it reaches it.
    if (s.water == WaterKind::Ocean && (!(s.height < kSeaLevel) || p.enclosed)) {
        s.water = WaterKind::None;
    }
    s.waterLevel = s.water == WaterKind::None ? s.height : kSeaLevel;
    if (p.cover > 0.0f) {
        buryFaces(p.cover, site, memos != nullptr ? memos->coastFace : nullptr);
    }
}

} // namespace terrain
