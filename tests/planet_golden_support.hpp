#pragma once

// Small worlds of the planet level for the tests, and its golden checksums.

#include "terrain/generation/planet/planet_level.hpp"

#include "golden_support.hpp"
#include "legacy_relief_support.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace golden {

// 2,097 x 1,049 km: 256 x 128 cells of 8,192 m.
inline const terrain::WorldSize kSmallPlanet{32.0 * 65536.0, 16.0 * 65536.0};

// The planetary relief (plates and macro layer) scaled down to the small world: fewer plates,
// shorter warps and belts, a macro wavelength of 1,000 km.
// On the legacy relief: the planetary preset is the default world since generator 3.
inline terrain::HeightfieldParams smallPlanetaryParams() {
    terrain::HeightfieldParams params =
        test_relief::onLegacyRelief(terrain::planetaryHeightfieldParams());
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

// The planet level as phases 11B and 12A built it, before the hydrographic network of task 11C
// (agent 31): the bands of its winds meeting at once, the receivers of its cells the steepest
// neighbor below them (no links through the passes). The scenes of the tests of other agents
// that stand on the rivers, the falls and the ergs of a planet level keep it, so that they still
// test, bit for bit and with their thresholds untouched, the rivers, falls and ergs they were
// written about (the smoothed climate moves them all). Their checks are also run, as measures,
// on the real planet of the default world (agent 31's report). Step 2 of task 11C moves them onto
// the default planet level with its new valleys.
inline terrain::PlanetParams beforeNetwork(terrain::PlanetParams params) {
    params.climate.bandJoin = 0.0;
    params.climate.bandShift = 0.0;
    params.links.enabled = false;
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
        relief.add(static_cast<std::uint64_t>(build.floor[c]), 1);
        relief.add(static_cast<std::uint64_t>(build.mountain[c]), 1);
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
                                          {0x3F4264CF06FB5493ULL, 0x523A1AE68E52922EULL,
                                           0x6F87ED895801BCEFULL, 0x564039DF6B6FB311ULL});
    const std::vector<Part> planetary =
        planetParts({"planet planetary relief", "planet planetary climate",
                     "planet planetary hydrology", "planet planetary level"},
                    smallBuild(smallPlanetaryParams(), 1),
                    {0x5567F4879C93953CULL, 0xCDB30A8703052579ULL, 0x99089D0C029CEA5AULL,
                     0xFAB952517A1A2172ULL});
    parts.insert(parts.end(), planetary.begin(), planetary.end());
    return parts;
}

} // namespace golden
