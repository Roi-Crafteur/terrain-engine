#pragma once

// Golden checksums of the surface (see golden_support.hpp): the layers of the Heightfield at
// 1 m, 1 km and 1000 km, with and without the macro and plate layers, the climate, biomes,
// materials and plant densities of the Ecology, and the tiles of the streamed world without
// caves at LOD 0 to 3.

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "golden_support.hpp"
#include "streaming_test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace golden {

namespace surface_detail {

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

inline void addSample(Checksum& sum, const terrain::HeightSample& s) {
    for (const float value :
         {s.continentalness, s.macroContinentalness, s.plateContinentalness, s.plateCollision,
          s.mountainMask, s.base, s.mountain, s.detail, s.height}) {
        sum.add(value);
    }
}

// Every layer of side x side samples, step meters apart, from (x0, z0).
inline std::uint64_t heightsChecksum(const terrain::Heightfield& field, double x0, double z0,
                                     double step, int side) {
    Checksum sum;
    for (int k = 0; k < side; ++k) {
        for (int i = 0; i < side; ++i) {
            addSample(sum, field.sampleAt(x0 + i * step, z0 + k * step));
        }
    }
    return sum.value();
}

// Climate, biome, material and plant densities of a surface of 48 x 48 samples.
inline std::uint64_t ecologyChecksum(const terrain::Heightfield& field,
                                     const terrain::Ecology& ecology, double centerX,
                                     double centerZ, double metersPerPixel) {
    const terrain::HeightmapRegion region{centerX, centerZ, metersPerPixel, 48};
    const terrain::EcologyMap map = ecology.map(terrain::surfaceFromHeightfield(field, region));
    Checksum sum;
    for (const terrain::EcologySample& s : map.samples) {
        for (const float value : {s.climate.baseTemperature, s.climate.humidity,
                                  s.climate.temperature, s.climate.slope, s.climate.exposure}) {
            sum.add(value);
        }
        sum.add(static_cast<std::uint64_t>(s.biome), 1);
        sum.add(static_cast<std::uint64_t>(s.material), 1);
        for (const float density : s.densities) {
            sum.add(density);
        }
    }
    return sum.value();
}

inline terrain::Heightfield planetField() {
    terrain::HeightfieldParams params;
    params.macroContinent = 0.25f;
    params.plates.amplitude = 0.5f;
    return terrain::Heightfield(kSeed, params);
}

} // namespace surface_detail

// Computes the parts of the Heightfield and the Ecology with their expected values.
inline std::vector<Part> heightfieldParts() {
    using namespace surface_detail;
    const terrain::Heightfield field(kSeed);
    const terrain::Heightfield planet = planetField();
    const terrain::Ecology ecology(kSeed);
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(field);
    const double mx = mountain.x * terrain::kChunkSizeMeters;
    const double mz = mountain.z * terrain::kChunkSizeMeters;
    return {
        {"heights 1 m", heightsChecksum(field, mx, mz, 1.0, 64), 0x2A033DD63F7AF26BULL},
        {"heights 1 km", heightsChecksum(field, -32000.0, -32000.0, 1000.0, 64),
         0xDD9EB5901BD3BCCFULL},
        {"heights 1000 km", heightsChecksum(field, -16e6, -16e6, 1e6, 32), 0xFFB9B9CE617F6238ULL},
        {"planet 1 km", heightsChecksum(planet, -32000.0, -32000.0, 1000.0, 48),
         0xECDE94DB56ACDB64ULL},
        {"planet 1000 km", heightsChecksum(planet, -16e6, -16e6, 1e6, 32), 0x0CCC12A0F965C50DULL},
        {"ecology 50 m", ecologyChecksum(field, ecology, mx, mz, 50.0), 0x3177B586E5EC8B46ULL},
        {"ecology 2 km", ecologyChecksum(field, ecology, 0.0, 0.0, 2000.0), 0x5B7DBFE54EF7A579ULL},
    };
}

// Computes the parts of the streamed world without caves with their expected values: the
// tiles of LOD 0 to 3 holding a mountain chunk, with and without seams, and a LOD 0 tile with
// plants.
inline std::vector<Part> worldParts() {
    using namespace surface_detail;
    const terrain::TerrainTiles tiles(kSeed);
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(tiles.heightfield());
    const terrain::ChunkCoord plants = streaming_test::plantChunk(tiles, 10);
    constexpr std::array<std::uint64_t, terrain::kLodCount> kExpected = {
        0x94031B6685B1D82EULL, 0x98711C081214B3A3ULL, 0x351C09104EBDB090ULL, 0xD1DDC28B9BAB6F2AULL};
    constexpr std::array<const char*, terrain::kLodCount> kNames = {
        "world tiles LOD 0", "world tiles LOD 1", "world tiles LOD 2", "world tiles LOD 3"};
    std::vector<Part> parts;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const terrain::TileKey key = terrain::tileContaining(mountain, lod);
        Checksum sum;
        addTile(sum, tiles.generate({key, 0}));
        addTile(sum, tiles.generate({key, terrain::kSeamPosX | terrain::kSeamNegXNegZ}));
        const auto index = static_cast<std::size_t>(lod);
        parts.push_back({kNames[index], sum.value(), kExpected[index]});
    }
    Checksum plantSum;
    addTile(plantSum, tiles.generate({{plants, 0}, 0}));
    parts.push_back({"world plants", plantSum.value(), 0x01CB0AD07C28EC71ULL});
    return parts;
}

} // namespace golden
