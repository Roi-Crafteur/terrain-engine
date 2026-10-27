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



// The lake over a point: the highest of the lakes of the cells around it whose water covers it.
LakeWater Hydrology::Impl::lakeAt(double x, double z, double ground) const {
    return lakeIn(lakesAround(planetCellAt(grid, x, z)), x, z, ground);
}

LakeList Hydrology::Impl::lakesAround(std::size_t cell) const {
    static_assert(water_detail::kLakeSearch == kMargin, "a lake is searched as far as it reaches");
    const int ci = grid.column(cell);
    const int cj = grid.row(cell);
    LakeList out;
    for (int dj = -kMargin; dj <= kMargin; ++dj) {
        for (int di = -kMargin; di <= kMargin; ++di) {
            const std::int32_t id =
                lakeOf(grid.index(grid.wrapColumn(ci + di), grid.wrapRow(cj + dj)));
            const auto end = out.ids.begin() + static_cast<std::ptrdiff_t>(out.count);
            if (id >= 0 && std::find(out.ids.begin(), end, id) == end) {
                out.ids[out.count++] = id;
            }
        }
    }
    return out;
}

LakeWater Hydrology::Impl::lakeIn(const LakeList& around, double x, double z,
                                  double ground) const {
    LakeWater out;
    for (std::size_t k = 0; k < around.count; ++k) {
        const std::int32_t id = around.ids[k];
        // The water of a lake never stands above its level on the planet level: no need to
        // flood it where the relief stands higher.
        const PlanetLake& planetLake = planet->lakes()[static_cast<std::size_t>(id)];
        if (!(ground < static_cast<double>(planetLake.level))) {
            continue;
        }
        const water::FineLake& lake = fineLakeOf(id);
        const float level = lake.level();
        if (!(ground < static_cast<double>(level)) ||
            (out.lake >= 0 && !(level > out.level || (level == out.level && id < out.lake))) ||
            !lake.covers(x, z)) {
            continue;
        }
        out.lake = id;
        out.level = level;
        out.kind = planetLake.overflowing || planetLake.frozen ? WaterKind::Lake
                                                               : WaterKind::SaltLake;
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
