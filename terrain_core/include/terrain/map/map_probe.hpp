#pragma once

#include "terrain/generation/biome.hpp"
#include "terrain/generation/water_kind.hpp"

#include <cstdint>

namespace terrain {

class MapWorldSource;

// The slopes of the biome of a probe, meters: those of the far horizon (HorizonParams::
// materialSlopeStep, the cells of the LOD 3 tiles).
inline constexpr double kMapProbeSlopeStep = 8.0;

// What the dev map says of the point under its cursor.
struct MapProbe {
    double x = 0.0; // canonical
    double z = 0.0;
    float ground = 0.0f;               // Heightfield::sampleAt with the water of the world
    WaterKind water = WaterKind::None; // the water over it
    float waterLevel = kNoWaterLevel;  // its surface where there is water
    Biome biome = Biome::Ocean;        // classified as the far horizon does (kMapProbeSlopeStep)
    float temperature = 0.0f;          // of the climate there, degrees Celsius
    float humidity = 0.0f;             // [0, 1]
    bool planet = false;               // the values below come from a planet level
    float discharge = 0.0f;            // m^3/s of the river of the planet cell, else 0
    std::int32_t lake = -1;            // the lake of the planet cell, or -1
    float precipitation = 0.0f;        // mm/yr of the planet level

    // The surface the player stands on: the water where there is some, else the ground.
    [[nodiscard]] float surface() const {
        return water == WaterKind::None ? ground : waterLevel;
    }
};

// The probe of the world of source at (x, z), any image: the same bits as the samples of the
// world (Heightfield::sampleAt of its field, with its hydrology when it has one). Thread-safe.
[[nodiscard]] MapProbe probeAt(const MapWorldSource& source, double x, double z);

} // namespace terrain
