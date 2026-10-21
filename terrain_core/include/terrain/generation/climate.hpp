#pragma once

#include "terrain/generation/terrain_surface.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain {

// Temperatures are in degrees Celsius, wavelengths in meters (one noise lattice cell of the
// first octave). The climate only reads the relief: it never changes a height.
struct ClimateParams {
    // Base temperature, the mean annual temperature at sea level, from the latitude and a
    // regional fbm with 4 octaves: equatorTemperature - (equatorTemperature - poleTemperature)
    // * s^1.5 + temperatureAmplitude * fbm, where s = sin^2(pi z / world.z) is 0 on the
    // equator (z = 0, the middle of the map) and 1 at the poles (z = +-world.z / 2, the
    // north-south seam). Cold at both edges, warm in the middle, with neither a jump nor a kink
    // at the seam: s is periodic and flat at the poles. About 9 degrees at 45 degrees of
    // latitude.
    double temperatureWavelength = 100000.0;
    float equatorTemperature = 27.0f;
    float poleTemperature = -23.0f;
    float temperatureAmplitude = 15.0f;

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
// The noise layers are continuous, so the climate has no seam between regions or chunks, and
// periodic on the world (WorldSize): (x, z) is brought back into its canonical range first.
class ClimateField {
public:
    // Throws std::invalid_argument for an invalid world (validate).
    explicit ClimateField(std::uint64_t seed, const ClimateParams& params = {},
                          const WorldSize& world = {});

    // Part of the base temperature given by the latitude of z alone (no fbm).
    [[nodiscard]] float latitudeTemperatureAt(double z) const;
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
    [[nodiscard]] const WorldSize& world() const { return world_; }

private:
    ClimateParams params_;
    WorldSize world_;
    std::uint64_t temperatureSeed_;
    std::uint64_t humiditySeed_;
};

} // namespace terrain
