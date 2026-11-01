#include "terrain/generation/settlement/geology.hpp"

#include "../planet/planet_parallel.hpp"
#include "geology_plates.hpp"
#include "geology_points.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/settlement/preference.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace terrain::settlement {

namespace {

double bump(double t) {
    if (std::abs(t) >= 1.0) {
        return 0.0;
    }
    const double u = 1.0 - t * t;
    return u * u;
}

struct Faults {
    double fault = 0.0;
    double major = 0.0;
    double divergence = 0.0;
};

Faults faultsAt(const GeographyPlates& plates, const GeologyRule& rule, double x, double z) {
    Faults f;
    const GeographyPlates::Near n = plates.nearAt(x, z);
    if (n.boundary < 0) {
        return f;
    }
    const geography::Boundary& b = plates.boundary(n.boundary);
    const double d = std::abs(n.towardB);
    const double width = rule.faultWidthKm * 1000.0;
    f.fault = bump(d / width);
    const double riftWidth = plates.geography().params().riftWidth;
    if (b.kind == BoundaryKind::Divergence) {
        f.divergence = bump(d / std::max(riftWidth, width));
        if (b.rift) {
            f.fault = std::max(f.fault, bump((d - riftWidth) / width));
        }
    } else if (b.kind == BoundaryKind::Transform) {
        f.major = bump(d / (rule.majorFaultWidthKm * 1000.0));
    }
    return f;
}

void landFaults(const PlanetGrid& grid, const GeographyPlates& plates, const GeologyRule& rule,
                int threads, Geology& g) {
    const planet::LandIndex& land = g.fault.index();
    planet::parallelRows(grid.nz, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const std::size_t cell = grid.index(i, j);
                if (!land.land(cell)) {
                    continue;
                }
                const Faults f = faultsAt(plates, rule, grid.centerX(i), grid.centerZ(j));
                g.fault.at(cell) = quantize(f.fault);
                g.majorFault.at(cell) = quantize(f.major);
                g.divergence.at(cell) = quantize(f.divergence);
            }
        }
    });
}

} // namespace

std::size_t Geology::memoryBytes() const {
    return volcanoes.capacity() * sizeof(Volcano) + craters.capacity() * sizeof(ArcaneCrater) +
           fault.values().capacity() + majorFault.values().capacity() +
           divergence.values().capacity();
}

double worldScale(const PlanetGrid& grid, const SettlementConfig& config) {
    return grid.periodX() * grid.periodZ() / 1.0e6 / config.referenceAreaKm2;
}

double torusDistance(double ax, double az, double bx, double bz, double periodX,
                     double periodZ) {
    double dx = std::abs(ax - bx);
    double dz = std::abs(az - bz);
    dx = std::fmod(dx, periodX);
    dz = std::fmod(dz, periodZ);
    dx = std::min(dx, periodX - dx);
    dz = std::min(dz, periodZ - dz);
    return dmath::hypot(dx, dz);
}

Geology buildGeology(const PlanetGrid& grid, const std::shared_ptr<const planet::LandIndex>& land,
                     const Geography& geo, std::uint64_t worldSeed,
                     const SettlementConfig& config, int threads) {
    const GeographyPlates plates(worldSeed, geo, threads);
    const double scale = worldScale(grid, config);
    Geology g;
    g.volcanoes = placeVolcanoes(plates, worldSeed, config.geology, scale, threads);
    g.craters = placeCraters(geo.world(), worldSeed, config.geology, scale);
    g.fault = planet::LandValues<std::uint8_t>(land, 0, 0);
    g.majorFault = planet::LandValues<std::uint8_t>(land, 0, 0);
    g.divergence = planet::LandValues<std::uint8_t>(land, 0, 0);
    landFaults(grid, plates, config.geology, threads, g);
    return g;
}

} // namespace terrain::settlement
