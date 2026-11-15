#pragma once

#include "terrain/generation/climate.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

// The zones of the monsoon (agent 49): a marking, never a biome nor a height. Its share at a
// column, in [0, 1], is the product of three smooth shares, so its edges wave:
//   - warm: the temperature at sea level (ClimateSample::baseTemperature), none at or under
//     warmLow, full from warmHigh;
//   - wet: the humidity of the climate (the planet level where there is one), none at or under
//     wetLow, full from wetHigh;
//   - the tropics: the latitude between bandInner and bandOuter degrees, fading over bandEdge on
//     both sides, read at a point moved along the meridians by a slow noise (warpDegrees, its
//     wavelength warpWavelength): never a band along a parallel.
// The planet level has no seasons: this is where the seasonal rains of a monsoon would fall
// (wet tropics away from the equator), for the dev map, F3 and the probe. The wetlands and the
// floodplains of agent 50 are to read it. Deterministic, thread-safe, periodic on the world.
namespace terrain {

struct MonsoonParams {
    float warmLow = 20.0f; // Celsius, at sea level
    float warmHigh = 24.0f;
    float wetLow = 0.45f; // humidity of the climate
    float wetHigh = 0.6f;
    double bandInner = 8.0; // degrees of latitude
    double bandOuter = 25.0;
    double bandEdge = 5.0;
    double warpDegrees = 4.0;
    double warpWavelength = 1'500'000.0; // meters
};

class Monsoon {
public:
    // Throws std::invalid_argument for an invalid world (validate) or parameters (an empty or
    // reversed range, a negative warp or edge).
    Monsoon(std::uint64_t seed, const WorldSize& world, const MonsoonParams& params = {});

    // The share of the monsoon at (x, z) of that climate, [0, 1]: 0 where it is too cold or too
    // dry, before any noise is read.
    [[nodiscard]] float shareAt(double x, double z, const ClimateSample& climate) const;
    // The latitude in degrees, [0, 90], of the point the band reads (warped).
    [[nodiscard]] double latitudeAt(double x, double z) const;
    [[nodiscard]] const MonsoonParams& params() const { return params_; }

private:
    MonsoonParams params_;
    WorldSize world_;
    std::uint64_t warpSeed_;
};

} // namespace terrain
