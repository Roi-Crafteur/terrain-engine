#pragma once

// Small worlds of the planet level for the tests, and its golden checksums.

#include "terrain/generation/planet/planet_level.hpp"

#include "golden_support.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace golden {

// 2,097 x 1,049 km: 256 x 128 cells of 8,192 m.
inline const terrain::WorldSize kSmallPlanet{32.0 * 65536.0, 16.0 * 65536.0};

// The planetary relief (plates and macro layer) scaled down to the small world: fewer plates,
// shorter warps and belts, a macro wavelength of 1,000 km.
inline terrain::HeightfieldParams smallPlanetaryParams() {
    terrain::HeightfieldParams params = terrain::planetaryHeightfieldParams();
    params.macroContinentWavelength = 1'000'000.0;
    terrain::VoronoiParams& plates = params.plates;
    plates.minPlates = 4;
    plates.maxPlates = 8;
    plates.warpWavelength = 600'000.0;
    plates.warpAmplitude = 100'000.0;
    plates.detailWarpWavelength = 120'000.0;
    plates.detailWarpAmplitude = 10'000.0;
    plates.coastWidth = 50'000.0;
    plates.coastNoiseWavelength = 150'000.0;
    plates.collisionWidth = 60'000.0;
    plates.riftWidth = 20'000.0;
    plates.riftFlankWidth = 60'000.0;
    plates.junctionFade = 20'000.0;
    return params;
}

inline terrain::PlanetParams smallPlanetParams(int threads, int samplesPerAxis = 1) {
    terrain::PlanetParams params;
    params.threads = threads;
    params.samplesPerAxis = samplesPerAxis;
    return params;
}

inline terrain::PlanetBuild smallBuild(const terrain::HeightfieldParams& relief, int threads,
                                       int samplesPerAxis = 1) {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, relief, kSmallPlanet);
    return terrain::planet::buildPlanet(field, smallPlanetParams(threads, samplesPerAxis));
}

inline void addLakes(Checksum& sum, const std::vector<terrain::PlanetLake>& lakes) {
    sum.add(static_cast<std::uint64_t>(lakes.size()));
    for (const terrain::PlanetLake& lake : lakes) {
        sum.add(lake.node);
        sum.add(lake.overflowing);
        sum.add(lake.playa);
        sum.add(lake.level);
        sum.add(static_cast<std::uint64_t>(lake.pass), 4);
        sum.add(static_cast<std::uint64_t>(lake.partner), 4);
        sum.add(static_cast<std::uint64_t>(lake.cells), 4);
        sum.add(static_cast<std::uint64_t>(lake.inflow));
        sum.add(static_cast<std::uint64_t>(lake.outflow));
    }
}

// Checksums of each stage of a build of the small world, then of the compact level.
inline std::vector<Part> planetParts(const std::array<const char*, 4>& names,
                                     const terrain::PlanetBuild& build,
                                     const std::array<std::uint64_t, 4>& expected) {
    Checksum relief;
    Checksum climate;
    Checksum hydrology;
    for (std::size_t c = 0; c < build.grid.cells(); ++c) {
        relief.add(build.height[c]);
        relief.add(static_cast<std::uint64_t>(build.ocean[c]), 1);
        climate.add(build.climate.precipitation[c]);
        hydrology.add(static_cast<std::uint64_t>(build.receiver[c]), 1);
        hydrology.add(build.lakeOfCell[c]);
        hydrology.add(build.discharge[c]);
    }
    for (const float t : build.climate.rowTemperature) {
        climate.add(t);
    }
    addLakes(hydrology, build.lakes);
    hydrology.add(static_cast<std::uint64_t>(build.budget.runoff));
    hydrology.add(static_cast<std::uint64_t>(build.budget.toOcean));
    terrain::PlanetBuild copy = build;
    const terrain::PlanetLevel level(std::move(copy), terrain::PlanetParams{});
    return {{names[0], relief.value(), expected[0]},
            {names[1], climate.value(), expected[1]},
            {names[2], hydrology.value(), expected[2]},
            {names[3], level.checksum(), expected[3]}};
}

// Golden parts of the planet level: the small world with the default relief (2 x 2 samples per
// cell) and with the scaled planetary relief, on one thread (so that the FTZ/DAZ mode of the
// calling thread applies to every stage).
inline std::vector<Part> planetGoldenParts() {
    std::vector<Part> parts = planetParts({"planet default relief", "planet default climate",
                                           "planet default hydrology", "planet default level"},
                                          smallBuild(terrain::HeightfieldParams{}, 1, 2),
                                          {0x48EF02B66C4B66A1ULL, 0xF68E69FB5F59D66DULL,
                                           0xEC9E09278A9ED9EAULL, 0xE1311536D4F4FC0FULL});
    const std::vector<Part> planetary =
        planetParts({"planet planetary relief", "planet planetary climate",
                     "planet planetary hydrology", "planet planetary level"},
                    smallBuild(smallPlanetaryParams(), 1),
                    {0x1C5872A57E9256C3ULL, 0xE5FCEAB898B8F074ULL, 0x2E0836B5BA84A5BCULL,
                     0xA7B7C31294354827ULL});
    parts.insert(parts.end(), planetary.begin(), planetary.end());
    return parts;
}

} // namespace golden
