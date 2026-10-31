#pragma once

// The worlds of the tests of the dev map on the generation (map_source.hpp): the default world
// without a planet level, and the small world with its water (golden::kSmallPlanet).

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/map/map_render.hpp"
#include "terrain/map/map_source.hpp"
#include "terrain/map/map_style.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "planet_golden_support.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace map_world {

inline constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

inline int threads() {
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

// The default world, the relief alone (no planet file).
struct DryWorld {
    terrain::Heightfield field{kSeed};
    terrain::Ecology ecology{kSeed};
    terrain::MapWorldSource source{field, ecology};
};

inline const DryWorld& dry() {
    static const DryWorld world;
    return world;
}

// The planet level of the small world, built once.
inline std::shared_ptr<const terrain::PlanetLevel> smallPlanet() {
    static const std::shared_ptr<const terrain::PlanetLevel> planet = [] {
        const terrain::Heightfield relief(kSeed, terrain::HeightfieldParams{}, golden::kSmallPlanet);
        return std::make_shared<const terrain::PlanetLevel>(
            terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threads())),
            golden::smallPlanetParams(threads()));
    }();
    return planet;
}

// The small world with its water, as a game session builds it: a hydrology of its own (no course
// computed yet when it is made), its tiles and the source of their map.
struct WetWorld {
    terrain::Heightfield relief{kSeed, terrain::HeightfieldParams{}, golden::kSmallPlanet};
    std::shared_ptr<const terrain::PlanetLevel> planet = smallPlanet();
    std::shared_ptr<const terrain::Hydrology> hydrology =
        std::make_shared<const terrain::Hydrology>(kSeed, relief, planet);
    terrain::TerrainTiles tiles{kSeed, terrain::HeightfieldParams{}, terrain::EcologyParams{},
                                std::nullopt, golden::kSmallPlanet, hydrology};
    terrain::MapWorldSource source{tiles};
};

// The lip of the first fall of at least 20 m of the small world, and its reach (a fresh
// hydrology, so that the worlds of the tests start with no course computed).
struct FallPlace {
    double x = 0.0;
    double z = 0.0;
    float drop = 0.0f;
};

inline FallPlace firstFall() {
    static const FallPlace place = [] {
        const terrain::Heightfield relief(kSeed, terrain::HeightfieldParams{}, golden::kSmallPlanet);
        const terrain::Hydrology h(kSeed, relief, smallPlanet());
        for (std::size_t r = 0; r < h.reachCount(); ++r) {
            for (const terrain::RiverFall& f : h.reach(r).falls) {
                if (f.top - f.bottom >= 20.0f) {
                    return FallPlace{f.x, f.z, f.top - f.bottom};
                }
            }
        }
        return FallPlace{};
    }();
    return place;
}

// A frame of size x size pixels of step meters centered on (x, z).
inline terrain::MapFrame frameAround(double x, double z, double step, int size,
                                     const terrain::WorldSize& world = {}) {
    const double side = step * size;
    return terrain::MapFrame({x - 0.5 * side, z - 0.5 * side, side, side}, size, size, world);
}

// The samples of a frame.
inline std::vector<terrain::MapSample> samplesOf(const terrain::MapSampler& sampler,
                                                 const terrain::MapFrame& frame, int threads,
                                                 terrain::MapSampleStats* stats = nullptr) {
    std::vector<terrain::MapSample> out(terrain::mapGridSize(frame));
    terrain::MapSampleStats local;
    sampler.sample(frame, out, threads, stats != nullptr ? *stats : local);
    return out;
}

} // namespace map_world
