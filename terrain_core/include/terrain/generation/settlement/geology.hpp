#pragma once

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain {
class Geography;
}

// The geology in data, phase 1 (docs/PEUPLEMENT.md, section 1): the faults along the boundaries
// of the existing tectonic plates, the volcanoes and the arcane craters, as data only (positions,
// kinds, intensities). The relief is never changed: they feed the Mana, the resources and the
// danger. Phase 2 (a volcanic agent changing the world) will take the same positions back from
// the same seed.
namespace terrain::settlement {

enum class VolcanoKind : std::uint8_t {
    Arc,     // behind a subduction, on the overriding plate
    Rift,    // along a continental rift
    Ridge,   // where a mid-ocean ridge reaches the land
    Hotspot, // isolated, anywhere
};

// A point in meters of the world, in its canonical range.
struct Volcano {
    double x = 0.0;
    double z = 0.0;
    VolcanoKind kind = VolcanoKind::Arc;
    bool active = false;
    bool large = false;
};

struct ArcaneCrater {
    double x = 0.0;
    double z = 0.0;
    double radiusKm = 0.0;
    bool major = false;
};

struct Geology {
    std::vector<Volcano> volcanoes;
    std::vector<ArcaneCrater> craters;
    // Per land cell, 0 to 255: the faults (every boundary of the plates and the edges of the
    // rifts, fading over faultWidthKm), the major faults (the transform boundaries, over
    // majorFaultWidthKm) and the diverging boundaries.
    planet::LandValues<std::uint8_t> fault;
    planet::LandValues<std::uint8_t> majorFault;
    planet::LandValues<std::uint8_t> divergence;

    [[nodiscard]] std::size_t memoryBytes() const;
};

// The geology of the world of geo (the geography of the Heightfield of worldSeed) on the land
// cells of grid. The counts of config.geology are those of the reference planet, scaled to the
// area of the world. The same bits whatever threads.
[[nodiscard]] Geology buildGeology(const PlanetGrid& grid,
                                   const std::shared_ptr<const planet::LandIndex>& land,
                                   const Geography& geo, std::uint64_t worldSeed,
                                   const SettlementConfig& config, int threads);

// The share of the area of the reference planet a world covers.
[[nodiscard]] double worldScale(const PlanetGrid& grid, const SettlementConfig& config);

// The shortest distance in meters between two points of a wrapped world.
[[nodiscard]] double torusDistance(double ax, double az, double bx, double bz, double periodX,
                                   double periodZ);

} // namespace terrain::settlement
