#pragma once

// The default world with its planet file, as Unreal loads it (agent 33): the rivers truncate the
// inlets and remove the reefs crossing their corridors, so that a place of the coasts proves
// nothing without them. The file is the one the fixture coast_planet_file builds once (or reads,
// when it holds the planet of this generator) with terrain_probe --planet, given to the tests by
// the environment variable TERRAIN_COAST_PLANET (tests/coast_tests.cmake).

#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/storage/planet_file.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

namespace coast_planet {

inline const char* filePath() {
    const char* path = std::getenv("TERRAIN_COAST_PLANET");
    if (path == nullptr) {
        std::fprintf(stderr, "TERRAIN_COAST_PLANET: no planet file (the fixture coast_planet)\n");
        std::abort();
    }
    return path;
}

// The water of the default world: its planet level read from the file.
inline std::shared_ptr<const terrain::Hydrology> hydrology() {
    static const std::shared_ptr<const terrain::Hydrology> h = [] {
        static const terrain::Heightfield relief(terrain::kDefaultWorldSeed);
        auto level = std::make_shared<const terrain::PlanetLevel>(terrain::loadPlanetFile(
            filePath(), terrain::kDefaultWorldSeed, terrain::PlanetParams{}, terrain::WorldSize{}));
        return std::make_shared<const terrain::Hydrology>(terrain::kDefaultWorldSeed, relief,
                                                         std::move(level));
    }();
    return h;
}

// The relief of the default world with its water.
inline const terrain::Heightfield& field() {
    static const terrain::Heightfield f =
        terrain::Heightfield(terrain::kDefaultWorldSeed).withHydrology(hydrology());
    return f;
}

// Whether a river or a lake keeps the relief without the coasts at the canonical (x, z), in part
// (Heightfield::leaveValleys: the coasts fade out over 60 m of the clearance of their valleys):
// its valley is no shape of the coasts.
inline bool nearRiver(double x, double z) {
    constexpr double kClearance = 60.0;
    const terrain::Hydrology& h = *hydrology();
    return h.mayMatter(x, z) &&
           h.clearance(x, z, static_cast<double>(h.relief().heightAt(x, z))) < kClearance;
}

// Whether the line from (x, z) along (dx, dz), from from to from + length meters, crosses such a
// valley, read every step meters.
inline bool lineNearRiver(double x, double z, double dx, double dz, double from, double length,
                          double step) {
    for (double d = from; d <= from + length; d += step) {
        if (nearRiver(x + d * dx, z + d * dz)) {
            return true;
        }
    }
    return false;
}

// Its tiles, with the rock in three dimensions.
inline const terrain::TerrainTiles& tiles() {
    static const terrain::TerrainTiles t(terrain::kDefaultWorldSeed, {}, {}, std::nullopt, {},
                                         hydrology(), true);
    return t;
}

// Its tiles of the relief alone (a height per column).
inline const terrain::TerrainTiles& reliefTiles() {
    static const terrain::TerrainTiles t(terrain::kDefaultWorldSeed, {}, {}, std::nullopt, {},
                                         hydrology(), false);
    return t;
}

// The highest the rock of its tiles stands within reach meters of (x, z) along x and z, every
// 2 m (the top of the spire of a stack, which may lean off its middle).
inline double highestGround(double x, double z, double reach) {
    double top = -1e9;
    for (double dz = -reach; dz <= reach; dz += 2.0) {
        for (double dx = -reach; dx <= reach; dx += 2.0) {
            top = std::max(top, static_cast<double>(tiles().groundAt(x + dx, z + dz)));
        }
    }
    return top;
}

// The top of the spire of a stack: the highest ground within kSpireMiddle of its radius of its
// middle (a spire may lean off it).
inline constexpr double kSpireMiddle = 0.6;
inline double stackTop(const terrain::CoastStack& s) {
    return highestGround(s.x, s.z, kSpireMiddle * static_cast<double>(s.radius));
}

} // namespace coast_planet
