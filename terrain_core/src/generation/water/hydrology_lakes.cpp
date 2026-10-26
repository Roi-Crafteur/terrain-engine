// The lakes over the relief (hydrology.hpp). The planet level knows a lake by its cells and its
// level; over the relief, the lake is a fine lake (fine_lake.hpp) flooded from the floors of its
// cells, within kMargin cells of them: wherever the relief holds its water, as far as the crest
// of the sill that the river leaving it cuts; lower where the relief lets the water out sooner.
#include "terrain/generation/water/hydrology.hpp"

#include "hydrology_impl.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace terrain {

namespace {

// The water of a lake reaches at most kMargin cells beyond its own (as far as the crest of a
// sill between the next cell and the one after it).
constexpr int kMargin = 2;

} // namespace

void Hydrology::Impl::indexLakes() {
    const std::size_t count = planet->lakes().size();
    lakeStart.assign(count + 1, 0);
    for (const PlanetCells::LakeCell& lc : planet->cells().lakeCells) {
        if (lakes.has(lc.cell)) {
            ++lakeStart[static_cast<std::size_t>(lc.lake) + 1];
        }
    }
    for (std::size_t l = 0; l < count; ++l) {
        lakeStart[l + 1] += lakeStart[l];
    }
    lakeCellList.resize(lakeStart.back());
    std::vector<std::uint32_t> fill(lakeStart.begin(), lakeStart.end() - 1);
    for (const PlanetCells::LakeCell& lc : planet->cells().lakeCells) {
        if (lakes.has(lc.cell)) {
            lakeCellList[fill[static_cast<std::size_t>(lc.lake)]++] = lc.cell;
        }
    }
    fineLakes.resize(count);
}

const water::FineLake& Hydrology::Impl::fineLakeOf(std::int32_t lake) const {
    return fineLakes.get(static_cast<std::size_t>(lake), [&] { return makeFineLake(lake); });
}

// The fine lake of a lake: seeded at the floors of its cells, in the images of its first cell.
water::FineLake Hydrology::Impl::makeFineLake(std::int32_t lake) const {
    const auto l = static_cast<std::size_t>(lake);
    const std::uint32_t first = lakeCellList[lakeStart[l]];
    const double ox = grid.centerX(grid.column(first));
    const double oz = grid.centerZ(grid.row(first));
    const int floorSamples = planet->params().floorSamples;
    std::vector<water::FineLake::Seed> seeds;
    std::vector<std::size_t> allowed;
    water::LakeWindow w{std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity()};
    for (std::uint32_t k = lakeStart[l]; k < lakeStart[l + 1]; ++k) {
        const std::size_t c = lakeCellList[k];
        const std::array<double, 2> p =
            planet::floorPoint(grid, c, planet::floorOf(relief, grid, c, floorSamples),
                               floorSamples);
        seeds.push_back({ox + noise::wrapCanonical(p[0] - ox, grid.periodX()),
                         oz + noise::wrapCanonical(p[1] - oz, grid.periodZ())});
        const double cx =
            ox + noise::wrapCanonical(grid.centerX(grid.column(c)) - ox, grid.periodX());
        const double cz = oz + noise::wrapCanonical(grid.centerZ(grid.row(c)) - oz, grid.periodZ());
        const double reach = (0.5 + kMargin) * grid.cellSize;
        w = {std::min(w.x0, cx - reach), std::min(w.z0, cz - reach), std::max(w.x1, cx + reach),
             std::max(w.z1, cz + reach)};
        for (int dj = -kMargin; dj <= kMargin; ++dj) {
            for (int di = -kMargin; di <= kMargin; ++di) {
                allowed.push_back(grid.index(grid.wrapColumn(grid.column(c) + di),
                                             grid.wrapRow(grid.row(c) + dj)));
            }
        }
    }
    std::sort(allowed.begin(), allowed.end());
    allowed.erase(std::unique(allowed.begin(), allowed.end()), allowed.end());
    const auto inside = [&](double x, double z) {
        return std::binary_search(allowed.begin(), allowed.end(), planetCellAt(grid, x, z));
    };
    return {relief, seeds, planet->lakes()[l].level, w, inside, grid.periodX(), grid.periodZ()};
}

LakeWater Hydrology::lakeAt(double x, double z, double ground) const {
    return impl_->lakeAt(x, z, ground);
}

// The lake over a point: the highest of the lakes of the cells around it whose water covers it.
LakeWater Hydrology::Impl::lakeAt(double x, double z, double ground) const {
    const Impl& m = *this;
    const std::size_t cell = planetCellAt(m.grid, x, z);
    const int ci = m.grid.column(cell);
    const int cj = m.grid.row(cell);
    std::array<std::int32_t, (2 * kMargin + 1) * (2 * kMargin + 1)> seen{};
    std::size_t count = 0;
    LakeWater out;
    for (int dj = -kMargin; dj <= kMargin; ++dj) {
        for (int di = -kMargin; di <= kMargin; ++di) {
            const std::int32_t id =
                m.lakeOf(m.grid.index(m.grid.wrapColumn(ci + di), m.grid.wrapRow(cj + dj)));
            const auto end = seen.begin() + static_cast<std::ptrdiff_t>(count);
            if (id < 0 || std::find(seen.begin(), end, id) != end) {
                continue;
            }
            seen[count++] = id;
            // The water of a lake never stands above its level on the planet level: no need to
            // flood it where the relief stands higher.
            const PlanetLake& planetLake = m.planet->lakes()[static_cast<std::size_t>(id)];
            if (!(ground < static_cast<double>(planetLake.level))) {
                continue;
            }
            const water::FineLake& lake = m.fineLakeOf(id);
            const float level = lake.level();
            if (!(ground < static_cast<double>(level)) ||
                (out.lake >= 0 &&
                 !(level > out.level || (level == out.level && id < out.lake))) ||
                !lake.covers(x, z)) {
                continue;
            }
            out.lake = id;
            out.level = level;
            out.kind = planetLake.overflowing || planetLake.frozen ? WaterKind::Lake
                                                                   : WaterKind::SaltLake;
        }
    }
    return out;
}

float Hydrology::lakeLevel(std::int32_t lake) const {
    return impl_->levelOf(lake);
}

namespace {

// The humidity of the planet level of a hydrology (planetHumidity).
class PlanetHumidity final : public HumiditySource {
public:
    explicit PlanetHumidity(std::shared_ptr<const Hydrology> hydrology)
        : hydrology_(std::move(hydrology)) {}
    [[nodiscard]] float humidityAt(double x, double z) const override {
        return hydrology_->planet().humidityAt(x, z);
    }

private:
    std::shared_ptr<const Hydrology> hydrology_;
};

} // namespace

std::shared_ptr<const HumiditySource> planetHumidity(std::shared_ptr<const Hydrology> hydrology) {
    if (!hydrology) {
        return nullptr;
    }
    return std::make_shared<const PlanetHumidity>(std::move(hydrology));
}

} // namespace terrain
