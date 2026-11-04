#pragma once

// The worlds of the tests of the seas (agent 51, seas.hpp): the small world and its planet level
// (dunes_test::small(), with its ergs), its water with the seas and without them, their tiles;
// bit for bit comparisons of samples and tiles.

#include "dunes_support.hpp"

#include "terrain/generation/seas.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>

namespace seas_test {

inline constexpr std::uint64_t kSeed = dunes_test::kSeed;
// HeightSample::continentalness times this is the coast signal (heightfield_geography.cpp).
inline constexpr double kContinentalScale = 1'500'000.0;

inline double coastOf(const terrain::HeightSample& s) {
    return static_cast<double>(s.continentalness) * kContinentalScale;
}

inline std::shared_ptr<const terrain::Hydrology>
hydrologyOf(bool seas, const terrain::SeaParams& params = {}) {
    terrain::HydrologyParams p;
    p.ergs = dunes_test::smallErgParams();
    p.seas = params;
    p.seas.enabled = seas;
    return std::make_shared<const terrain::Hydrology>(kSeed, dunes_test::small().relief,
                                                      dunes_test::small().planet, p);
}

struct Worlds {
    std::shared_ptr<const terrain::Hydrology> on = hydrologyOf(true);
    std::shared_ptr<const terrain::Hydrology> off = hydrologyOf(false);
    terrain::TerrainTiles withSeas{
        kSeed,        terrain::HeightfieldParams{}, terrain::EcologyParams{},
        std::nullopt, golden::kSmallPlanet,         on};
    terrain::TerrainTiles without{
        kSeed,        terrain::HeightfieldParams{}, terrain::EcologyParams{},
        std::nullopt, golden::kSmallPlanet,         off};
};

inline const Worlds& worlds() {
    static const Worlds w;
    return w;
}

inline bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// Every field of two samples, bit for bit.
inline bool sameSample(const terrain::HeightSample& a, const terrain::HeightSample& b) {
    return same(a.continentalness, b.continentalness) &&
           same(a.macroContinentalness, b.macroContinentalness) &&
           same(a.plateContinentalness, b.plateContinentalness) &&
           same(a.plateCollision, b.plateCollision) && same(a.mountainMask, b.mountainMask) &&
           same(a.relief, b.relief) && same(a.rift, b.rift) && same(a.base, b.base) &&
           same(a.regional, b.regional) && same(a.mountain, b.mountain) &&
           same(a.detail, b.detail) && same(a.height, b.height) && a.water == b.water &&
           same(a.waterLevel, b.waterLevel) && same(a.dunes, b.dunes) && same(a.erg, b.erg) &&
           same(a.ergFloor, b.ergFloor) && same(a.rock, b.rock);
}

inline terrain::TileData tileAt(const terrain::TerrainTiles& tiles, double x, double z, int lod) {
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
    return tiles.generate({terrain::tileContaining(chunk, lod), 0});
}

// The mesh, the blend of the surface, the water and the rock of two tiles, bit for bit.
inline bool sameTile(const terrain::TileData& a, const terrain::TileData& b) {
    if (a.mesh.vertices.size() != b.mesh.vertices.size() || a.mesh.indices != b.mesh.indices ||
        a.waterLevels.size() != b.waterLevels.size() || a.rock.size() != b.rock.size()) {
        return false;
    }
    for (std::size_t n = 0; n < a.mesh.vertices.size(); ++n) {
        const terrain::Vec3f u = a.mesh.vertices[n];
        const terrain::Vec3f v = b.mesh.vertices[n];
        if (!same(u.x, v.x) || !same(u.y, v.y) || !same(u.z, v.z)) {
            return false;
        }
    }
    for (std::size_t n = 0; n < a.waterLevels.size(); ++n) {
        if (!same(a.waterLevels[n], b.waterLevels[n])) {
            return false;
        }
    }
    for (std::size_t n = 0; n < a.rock.size(); ++n) {
        if (!same(a.rock[n], b.rock[n])) {
            return false;
        }
    }
    return a.surfaceBlend == b.surfaceBlend && a.waterColumns.size() == b.waterColumns.size() &&
           a.plants.size() == b.plants.size();
}

} // namespace seas_test
