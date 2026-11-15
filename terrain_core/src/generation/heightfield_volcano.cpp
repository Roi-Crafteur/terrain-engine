// The volcanoes over the relief (agent 45, volcanoes.hpp): after the sea of the geography and
// before the planet level, so that the planet level, its rivers and its lakes find them.
#include "terrain/generation/heightfield.hpp"

#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include "heightfield_layers.hpp"
#include "volcano_forms.hpp"
#include "volcano_relief.hpp"
#include "volcano_water.hpp"
#include "wall_faces.hpp"
#include "coast_faces.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace terrain {

struct Heightfield::Volcanic {
    explicit Volcanic(std::size_t count) : details(count) {}

    // The details of volcano i of field, read once over the relief without the volcanoes and the
    // coasts (the same in every copy of field).
    const volcano_relief::Detail& detail(const Heightfield& field, std::size_t i) const {
        const volcano_relief::GroundAt ground = [&field](double gx, double gz) {
            Memos bare;
            bare.bare = true;
            bare.noVolcanoes = true;
            const HeightSample g = field.reliefWith(gx, gz, &bare);
            return volcano_relief::Ground{static_cast<double>(g.height),
                                          static_cast<double>(g.regional)};
        };
        return details.get(i, field.volcanoes_->sites()[i], ground);
    }

    volcano_relief::Details details;
};

std::shared_ptr<const Heightfield::Volcanic> Heightfield::makeVolcanic(const Volcanoes& volcanoes) {
    return std::make_shared<const Volcanic>(volcanoes.sites().size());
}

struct Heightfield::VolcanicWater {
    explicit VolcanicWater(std::size_t count)
        : flags(std::make_unique<std::once_flag[]>(count)), drained(count) {}

    // Whether the rivers of field drain basin b of volcano i: a valley, pond or lake of the rivers
    // over a point of its crest (read over the relief the rivers were found over, the same in
    // every copy of field). The maars that share a lake share it.
    bool isDrained(const Heightfield& field, std::size_t i, std::size_t b) const {
        std::call_once(flags[i], [&]() {
            const VolcanoSite& v = field.volcanoes_->sites()[i];
            const volcano_relief::Detail& d = field.volcanic_->detail(field, i);
            const Hydrology& h = *field.hydrology_;
            const std::function<bool(double, double)> carved = [&](double dx, double dz) {
                const double x = noise::wrapCanonical(v.x + dx, field.world_.x);
                const double z = noise::wrapCanonical(v.z + dz, field.world_.z);
                return h.mayMatter(x, z) &&
                       !(h.clearance(x, z, static_cast<double>(h.relief().heightAt(x, z))) > 0.0);
            };
            std::vector<std::uint8_t>& out = drained[i];
            out.assign(d.basins.size(), 0);
            for (std::size_t k = 0; k < d.basins.size(); ++k) {
                if (volcano_water::drained(d.basins[k], carved)) {
                    out[d.basins[k].group] = 1;
                }
            }
            for (std::size_t k = 0; k < d.basins.size(); ++k) {
                out[k] = out[d.basins[k].group];
            }
        });
        return drained[i][b] != 0;
    }

    std::unique_ptr<std::once_flag[]> flags;
    mutable std::vector<std::vector<std::uint8_t>> drained;
};

std::shared_ptr<const Heightfield::VolcanicWater>
Heightfield::makeVolcanicWater(const Volcanoes& volcanoes) {
    return std::make_shared<const VolcanicWater>(volcanoes.sites().size());
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
        const volcano_relief::Detail& d = volcanic_->detail(*this, i);
        volcano_relief::apply(v, d, o.dx, o.dz, footprint_, p);
    }
    const auto share =
        static_cast<std::uint8_t>(std::lround(std::clamp(p.groundShare, 0.0f, 1.0f) * 255.0f));
    s.volcanicShare = share;
    s.volcanicGround = share > 0 ? p.ground : VolcanicGround::None;
    if (!p.touched) {
        return; // the ground of a fumarole alone
    }
    const auto height = static_cast<float>(p.height);
    s.base += height - s.height;
    s.height = height;
    s.volcanic = static_cast<std::uint8_t>(std::lround(std::clamp(p.cover, 0.0f, 1.0f) * 255.0f));
    // The sea where the geography puts it, but over the land the volcanoes raise (their islands)
    // and in their craters closed above it: a ground below 0 m is the sea only when it reaches it.
    if (s.water == WaterKind::Ocean && (!(s.height < kSeaLevel) || p.enclosed)) {
        s.water = WaterKind::None;
    } else if (s.water == WaterKind::None && p.flooded && s.height < kSeaLevel) {
        s.water = WaterKind::Ocean;
    }
    s.waterLevel = s.water == WaterKind::None ? s.height : kSeaLevel;
    if (p.cover > 0.0f) {
        buryFaces(p.cover, site, memos != nullptr ? memos->coastFace : nullptr);
    }
}

namespace {

// The canonical point at (dx, dz) from the center of a volcano.
struct Canonical {
    double x = 0.0;
    double z = 0.0;
};

Canonical canonicalOf(const WorldSize& world, const VolcanoSite& v, double dx, double dz) {
    return {noise::wrapCanonical(v.x + dx, world.x), noise::wrapCanonical(v.z + dz, world.z)};
}

bool inBox(const Canonical& c, double minX, double minZ, double maxX, double maxZ) {
    return !(c.x < minX) && c.x < maxX && !(c.z < minZ) && c.z < maxZ;
}

// Whether the reach of a volcano may touch the box (canonical, not wrapping) on the torus.
bool reachesBox(const WorldSize& world, const VolcanoSite& v, double minX, double minZ,
                double maxX, double maxZ) {
    const VolcanoOffset o =
        volcanoOffset(world, v.x, v.z, std::clamp(v.x, minX, maxX), std::clamp(v.z, minZ, maxZ));
    const auto reach = static_cast<double>(v.reach);
    return o.dx * o.dx + o.dz * o.dz < reach * reach || (maxX - minX) >= world.x ||
           (maxZ - minZ) >= world.z;
}

} // namespace

float Heightfield::volcanoTopSeen(double x, double z, double pitch) const {
    float seen = -std::numeric_limits<float>::infinity();
    if (!volcanoes_) {
        return seen;
    }
    const double half = 0.5 * pitch;
    // The volcanoes of the buckets of the corners of the square: a top inside a reach may stand in
    // the square of a point beyond it.
    std::vector<std::uint32_t> near;
    for (const double cx : {x - half, x + half}) {
        for (const double cz : {z - half, z + half}) {
            const double wx = noise::wrapCanonical(cx, world_.x);
            const double wz = noise::wrapCanonical(cz, world_.z);
            if (volcanoes_->mayHold(wx, wz)) {
                const std::span<const std::uint32_t> some = volcanoes_->near(wx, wz);
                near.insert(near.end(), some.begin(), some.end());
            }
        }
    }
    std::sort(near.begin(), near.end());
    near.erase(std::unique(near.begin(), near.end()), near.end());
    for (const std::uint32_t i : near) {
        const VolcanoSite& v = volcanoes_->sites()[i];
        const VolcanoOffset o = volcanoOffset(world_, v.x, v.z, x, z);
        const double reach = static_cast<double>(v.reach) + pitch;
        if (!(o.dx * o.dx + o.dz * o.dz < reach * reach)) {
            continue;
        }
        for (const volcano_relief::Top& t : volcanic_->detail(*this, i).tops) {
            if (!(std::abs(o.dx - t.x) > half) && !(std::abs(o.dz - t.z) > half)) {
                seen = std::max(seen, static_cast<float>(t.height));
            }
        }
    }
    return seen;
}

double Heightfield::volcanoLake(std::size_t i, std::size_t b, bool& salt) const {
    salt = false;
    if (!hydrology_ || !volcanicWater_ || volcanicWater_->isDrained(*this, i, b)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const VolcanoSite& v = volcanoes_->sites()[i];
    const volcano_relief::Basin& basin = volcanic_->detail(*this, i).basins[b];
    const Canonical c = canonicalOf(world_, v, basin.wetX, basin.wetZ);
    return volcano_water::lakeLevel(
        basin, static_cast<double>(hydrology_->planet().humidityAt(c.x, c.z)), salt);
}

void Heightfield::volcanoWater(double x, double z, HeightSample& s) const {
    if (!volcanoes_ || !hydrology_ || s.water == WaterKind::Ocean || !volcanoes_->mayHold(x, z)) {
        return;
    }
    const std::vector<VolcanoSite>& sites = volcanoes_->sites();
    for (const std::uint32_t i : volcanoes_->near(x, z)) {
        const VolcanoSite& v = sites[i];
        const VolcanoOffset o = volcanoOffset(world_, v.x, v.z, x, z);
        const auto reach = static_cast<double>(v.reach);
        if (!(o.dx * o.dx + o.dz * o.dz < reach * reach)) {
            continue;
        }
        const volcano_relief::Detail& d = volcanic_->detail(*this, i);
        for (std::size_t k = 0; k < d.basins.size(); ++k) {
            if (!volcano_water::within(d.basins[k], o.dx, o.dz)) {
                continue;
            }
            bool salt = false;
            const auto level = static_cast<float>(volcanoLake(i, k, salt));
            // A single level in the basin: over a lake of the planet level, the higher one.
            if (level > s.height && (s.water == WaterKind::None || level > s.waterLevel)) {
                s.water = salt ? WaterKind::SaltLake : WaterKind::Lake;
                s.waterLevel = level;
            }
        }
        const volcano_relief::Spring* pool = volcano_water::poolAt(d, o.dx, o.dz, footprint_);
        if (pool != nullptr && s.water == WaterKind::None &&
            static_cast<double>(s.height) < pool->level) {
            s.water = WaterKind::Lake;
            s.waterLevel = static_cast<float>(pool->level);
        }
    }
}

std::vector<VolcanoBasin> Heightfield::volcanoBasinsIn(double minX, double minZ, double maxX,
                                                       double maxZ) const {
    std::vector<VolcanoBasin> out;
    if (!volcanoes_) {
        return out;
    }
    const std::vector<VolcanoSite>& sites = volcanoes_->sites();
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const VolcanoSite& v = sites[i];
        if (!reachesBox(world_, v, minX, minZ, maxX, maxZ)) {
            continue;
        }
        const std::vector<volcano_relief::Basin>& basins = volcanic_->detail(*this, i).basins;
        for (std::size_t k = 0; k < basins.size(); ++k) {
            const volcano_relief::Basin& b = basins[k];
            const Canonical c = canonicalOf(world_, v, b.x, b.z);
            if (!inBox(c, minX, minZ, maxX, maxZ)) {
                continue;
            }
            VolcanoBasin out1;
            out1.x = c.x;
            out1.z = c.z;
            out1.radius = static_cast<float>(b.radius);
            out1.floor = static_cast<float>(b.floor);
            out1.spill = static_cast<float>(b.spill);
            out1.maar = b.maar;
            out1.volcano = static_cast<std::uint32_t>(i);
            bool salt = false;
            out1.level = static_cast<float>(volcanoLake(i, k, salt));
            if (!std::isnan(out1.level)) {
                out1.water = salt ? WaterKind::SaltLake : WaterKind::Lake;
            }
            out.push_back(out1);
        }
    }
    return out;
}

std::vector<GeothermalSite> Heightfield::geothermalIn(double minX, double minZ, double maxX,
                                                      double maxZ) const {
    std::vector<GeothermalSite> out;
    if (!volcanoes_) {
        return out;
    }
    const std::vector<VolcanoSite>& sites = volcanoes_->sites();
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const VolcanoSite& v = sites[i];
        if (!v.active || !reachesBox(world_, v, minX, minZ, maxX, maxZ)) {
            continue;
        }
        for (const volcano_relief::Spring& sp : volcanic_->detail(*this, i).springs) {
            const Canonical c = canonicalOf(world_, v, sp.x, sp.z);
            if (inBox(c, minX, minZ, maxX, maxZ)) {
                out.push_back({c.x, c.z, sp.kind, static_cast<float>(sp.radius),
                               static_cast<float>(sp.level), static_cast<std::uint32_t>(i)});
            }
        }
    }
    return out;
}

} // namespace terrain
