#include "geology_plates.hpp"

#include "../geography_grid.hpp"
#include "../geography_stages.hpp"
#include "../planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/world_size.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

namespace terrain::settlement {

namespace {

// The layer of the geography in the Heightfield (heightfield.cpp, enum Layer): kGeography.
constexpr std::uint64_t kGeographyLayer = 8;

} // namespace

std::uint64_t geographySeed(std::uint64_t worldSeed) {
    return noise::deriveSeed(worldSeed, kGeographyLayer);
}

geography::Crust crustOf(const Geography& geo) {
    geography::Crust c;
    const auto nodes = static_cast<std::size_t>(geo.columns()) * static_cast<std::size_t>(geo.rows());
    c.land.assign(nodes, 0);
    c.coast.assign(nodes, 0.0f);
    for (int j = 0; j < geo.rows(); ++j) {
        for (int i = 0; i < geo.columns(); ++i) {
            const std::size_t k = static_cast<std::size_t>(j) * static_cast<std::size_t>(geo.columns()) +
                                  static_cast<std::size_t>(i);
            c.coast[k] = geo.node(i, j, Geography::kCoast);
            c.land[k] = c.coast[k] > 0.0f ? 1 : 0;
        }
    }
    c.candidate = geo.crustCandidate();
    c.continents = geo.continents();
    return c;
}

GeographyPlates::GeographyPlates(std::uint64_t worldSeed, const Geography& geo, int threads)
    : geo_(&geo), seed_(geographySeed(worldSeed)),
      grid_(geography::makeGrid(geo.world(), geo.params().cellSize)), crust_(crustOf(geo)) {
    const std::uint64_t stream = noise::deriveSeed(seed_, geography::kPlates);
    plates_ = std::make_unique<TectonicPlates>(noise::deriveSeed(stream, 0), geo.params().plates,
                                               geo.world());
    fields_ = std::make_unique<geography::BoundaryFields>(stream, *plates_, grid_, crust_,
                                                          planet::resolveThreads(threads));
    landShares_ =
        geography::landShares(plates_->plates().size(), fields_->nearest(), fields_->land());
    boundaries_ =
        geography::drawBoundaries(noise::deriveSeed(stream, 3), geo.params(), *plates_, landShares_);
    offsetX_.assign(grid_.nodes(), 0.0);
    offsetZ_.assign(grid_.nodes(), 0.0);
    planet::parallelRows(grid_.nz, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid_.nx; ++i) {
                const geography::NodeFrame f = fields_->at(i, j);
                offsetX_[grid_.index(i, j)] = f.warped.x - grid_.x(i);
                offsetZ_[grid_.index(i, j)] = f.warped.z - grid_.z(j);
            }
        }
    });
}

GeographyPlates::Near GeographyPlates::nearWarped(double wx, double wz) const {
    const WorldSize& world = plates_->world();
    const double qx = noise::wrapCanonical(wx, world.x);
    const double qz = noise::wrapCanonical(wz, world.z);
    double best = std::numeric_limits<double>::max();
    double second = std::numeric_limits<double>::max();
    int a = 0;
    int b = 0;
    double ax = 0.0;
    double az = 0.0;
    double bx = 0.0;
    double bz = 0.0;
    const std::vector<Plate>& plates = plates_->plates();
    for (std::size_t k = 0; k < plates.size(); ++k) {
        const double dx = geography::shortOffset(plates[k].x - qx, world.x);
        const double dz = geography::shortOffset(plates[k].z - qz, world.z);
        const double d2 = dx * dx + dz * dz;
        if (d2 < best) {
            second = best;
            b = a;
            bx = ax;
            bz = az;
            best = d2;
            a = static_cast<int>(k);
            ax = dx;
            az = dz;
        } else if (d2 < second) {
            second = d2;
            b = static_cast<int>(k);
            bx = dx;
            bz = dz;
        }
    }
    Near n;
    n.a = a;
    n.b = b;
    const double spacing = dmath::hypot(bx - ax, bz - az);
    n.towardB = spacing > 0.0 ? (best - second) / (2.0 * spacing) : 0.0;
    n.boundary = plates_->boundaryIndex(a, b);
    return n;
}

GeographyPlates::Near GeographyPlates::nearAtNode(int i, int j) const {
    const std::size_t k = grid_.index(grid_.wrapI(i), grid_.wrapJ(j));
    return nearWarped(grid_.x(grid_.wrapI(i)) + offsetX_[k], grid_.z(grid_.wrapJ(j)) + offsetZ_[k]);
}

GeographyPlates::Near GeographyPlates::nearAt(double x, double z) const {
    const WorldSize& world = geo_->world();
    const double gx = wrapCoordinate(x + geo_->anchorX(), world.x);
    const double gz = wrapCoordinate(z, world.z);
    const double u = (gx - grid_.x(0)) / grid_.sizeX;
    const double v = (gz - grid_.z(0)) / grid_.sizeZ;
    const int i0 = static_cast<int>(std::floor(u));
    const int j0 = static_cast<int>(std::floor(v));
    const double tu = u - i0;
    const double tv = v - j0;
    const auto at = [&](const std::vector<double>& field, int di, int dj) {
        return field[grid_.index(grid_.wrapI(i0 + di), grid_.wrapJ(j0 + dj))];
    };
    const auto blend = [&](const std::vector<double>& f) {
        const double top = at(f, 0, 0) + (at(f, 1, 0) - at(f, 0, 0)) * tu;
        const double bottom = at(f, 0, 1) + (at(f, 1, 1) - at(f, 0, 1)) * tu;
        return top + (bottom - top) * tv;
    };
    return nearWarped(gx + blend(offsetX_), gz + blend(offsetZ_));
}

} // namespace terrain::settlement
