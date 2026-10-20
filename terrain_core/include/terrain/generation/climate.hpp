#pragma once

#include "terrain/generation/terrain_surface.hpp"

#include <cstdint>

namespace terrain {

// Temperatures are in degrees Celsius, wavelengths in meters (one noise lattice cell of the
// first octave). The climate only reads the relief: it never changes a height.
struct ClimateParams {
    // Base temperature, the mean annual temperature at sea level:
    // meanTemperature + temperatureAmplitude * fbm with 4 octaves.
    double temperatureWavelength = 100000.0;
    float meanTemperature = 14.0f;
    float temperatureAmplitude = 50.0f;

    // Humidity in [0, 1]: meanHumidity + humidityAmplitude * fbm with 4 octaves, minus
    // inlandDrying * smoothstep(inlandLow, inlandHigh, continentalness): the coasts are wetter
    // than the interiors of the continents.
    double humidityWavelength = 60000.0;
    float meanHumidity = 0.55f;
    float humidityAmplitude = 1.4f;
    float inlandDrying = 0.25f;
    float inlandLow = 0.1f;
    float inlandHigh = 0.6f;

    // Altitude correction: the temperature drops by lapseRate degrees per meter above sea level.
    float lapseRate = 0.0065f;

    // Exposure: dry ground facing the sun (south, +Z) gains up to exposureWarming degrees, ground
    // facing away from it loses as much.
    float exposureWarming = 3.0f;
};

// Climate of a column.
struct ClimateSample {
    float baseTemperature = 0.0f; // at sea level
    float humidity = 0.0f;        // [0, 1]
    float temperature = 0.0f;     // corrected by the altitude and the exposure
    float slope = 0.0f;           // |grad H|, rise over run
    float exposure = 0.0f;        // [-1, 1], see exposureOf
};

// Slope of the ground, rise over run: |grad H|.
[[nodiscard]] float slopeOf(float gradX, float gradZ);

// Exposure to the sun, which stands to the south (+Z): the component of the unit ground
// normal (-gradX, 1, -gradZ) / |.| toward +Z. 0 on flat ground, positive on slopes facing the
// south, negative on slopes facing the north, within (-1, 1).
[[nodiscard]] float exposureOf(float gradX, float gradZ);

// Deterministic climate: the same seed, parameters and inputs always give the same climate.
// The noise layers are continuous, so the climate has no seam between regions or chunks.
class ClimateField {
public:
    explicit ClimateField(std::uint64_t seed, const ClimateParams& params = {});

    [[nodiscard]] float baseTemperatureAt(double x, double z) const;
    // Humidity at (x, z) given the continentalness of the Heightfield there.
    [[nodiscard]] float humidityAt(double x, double z, float continentalness) const;
    // Temperature at altitude meters (the top of the column: the water over the ocean) on
    // ground of the given exposure: baseTemperature - lapseRate * max(altitude - kSeaLevel, 0)
    // + exposureWarming * exposure.
    [[nodiscard]] float temperatureAt(float baseTemperature, float altitude, float exposure) const;

    // Climate of a column of the surface at (x, z). The exposure only warms dry ground.
    [[nodiscard]] ClimateSample sample(double x, double z, const SurfaceSite& site) const;

    [[nodiscard]] const ClimateParams& params() const { return params_; }

private:
    ClimateParams params_;
    std::uint64_t temperatureSeed_;
    std::uint64_t humiditySeed_;
};

} // namespace terrain
