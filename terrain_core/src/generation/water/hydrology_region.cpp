// The regions of the queries (Hydrology::Region, hydrology.hpp): what the points of a box share,
// found once.
#include "terrain/generation/water/hydrology.hpp"

#include "hydrology_impl.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

namespace terrain {

namespace {

using water_detail::kSearch;
using water_detail::kStray;
using water_detail::kValleyReach;

// A region holds a box of at most kRegionMost meters on each side (a tile, a patch): beyond, what
// its points share is too little to be worth finding.
constexpr double kRegionMost = 4'096.0;
// The bounds of the box hold with kMargin meters to spare: far more than the rounding of the
// distances the points compute.
constexpr double kMargin = 1e-3;

} // namespace

Hydrology::Region::Region() = default;
Hydrology::Region::~Region() = default;
Hydrology::Region::Region(Region&&) noexcept = default;
Hydrology::Region& Hydrology::Region::operator=(Region&&) noexcept = default;

// The reaches in the order candidatesAt meets them around the cell, less those no point of the
// box may reach (mayReach), and the lakes of the cells around it.
bool Hydrology::Impl::partOf(const Region::Data& region, std::size_t cell,
                             Region::Data::Part& part) const {
    part.cell = cell;
    const double reach = 0.5 * dmath::hypot(region.maxX - region.minX, region.maxZ - region.minZ) +
                         kMargin;
    const int ci = grid.column(cell);
    const int cj = grid.row(cell);
    for (int dj = -kSearch; dj <= kSearch; ++dj) {
        for (int di = -kSearch; di <= kSearch; ++di) {
            const CellReaches here =
                reachesOfCell(grid.index(grid.wrapColumn(ci + di), grid.wrapRow(cj + dj)));
            for (std::size_t k = 0; k < here.count; ++k) {
                Region::Data::Reach e;
                e.reach = here.reach[k];
                e.start = startOf(e.reach);
                const water::PathPoint end = endOf(e.reach);
                const auto image = [](double v, double from, double period) {
                    return from + noise::wrapCanonical(v - from, period);
                };
                e.minX = image(region.minX, e.start.x, grid.periodX());
                e.maxX = image(region.maxX, e.start.x, grid.periodX());
                e.minZ = image(region.minZ, e.start.z, grid.periodZ());
                e.maxZ = image(region.maxZ, e.start.z, grid.periodZ());
                if (!(std::abs((e.maxX - e.minX) - (region.maxX - region.minX)) < 1.0 &&
                      std::abs((e.maxZ - e.minZ) - (region.maxZ - region.minZ)) < 1.0)) {
                    return false;
                }
                const double center = water::distanceToSegment(
                    0.5 * (e.minX + e.maxX), 0.5 * (e.minZ + e.maxZ), e.start.x, e.start.z,
                    end.x, end.z);
                if (center - reach > kValleyReach + kStray) {
                    continue;
                }
                e.everywhere = center + reach <= kValleyReach + kStray;
                part.reaches.push_back(std::move(e));
            }
        }
    }
    part.lakes = lakesAround(cell);
    return true;
}

// A part for each cell the box meets: those of the columns and rows of its corners (the box is
// narrower than a cell). Empty (the queries go point by point) when the images of a reach do not
// follow the canonical points across the box.
Hydrology::Region Hydrology::regionOf(double minX, double minZ, double maxX, double maxZ,
                                      double groundMin, double groundMax) const {
    Region out;
    const Impl& m = *impl_;
    if (!(maxX - minX <= kRegionMost && maxZ - minZ <= kRegionMost && groundMin <= groundMax)) {
        return out;
    }
    auto d = std::make_unique<Region::Data>();
    d->minX = minX;
    d->minZ = minZ;
    d->maxX = maxX;
    d->maxZ = maxZ;
    d->groundMin = groundMin;
    d->groundMax = groundMax;
    const std::size_t low = planetCellAt(m.grid, minX, minZ);
    const std::size_t high = planetCellAt(m.grid, maxX, maxZ);
    const std::array<int, 2> columns{m.grid.column(low), m.grid.column(high)};
    const std::array<int, 2> rows{m.grid.row(low), m.grid.row(high)};
    for (std::size_t b = 0; b < 2; ++b) {
        for (std::size_t a = 0; a < 2; ++a) {
            if ((a == 1 && columns[1] == columns[0]) || (b == 1 && rows[1] == rows[0])) {
                continue;
            }
            Region::Data::Part part;
            if (!m.partOf(*d, m.grid.index(columns[a], rows[b]), part)) {
                return out;
            }
            d->parts.push_back(std::move(part));
        }
    }
    out.data_ = std::move(d);
    return out;
}

LakeWater Hydrology::lakeAt(double x, double z, double ground, const Region* region) const {
    Region::Data* d = region != nullptr ? region->data_.get() : nullptr;
    const Region::Data::Part* part = d != nullptr ? d->partOf(impl_->grid, x, z, ground) : nullptr;
    if (part != nullptr) {
        return impl_->lakeIn(part->lakes, x, z, ground);
    }
    return impl_->lakeAt(x, z, ground);
}

} // namespace terrain
