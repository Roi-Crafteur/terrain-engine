#include "terrain/generation/climate.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// New layers go at the end so that the seeds of the existing ones never change.
enum Layer : std::uint64_t { kTemperature, kHumidity };

} // namespace

float slopeOf(float gradX, float gradZ) {
    return std::hypot(gradX, gradZ);
}

float exposureOf(float gradX, float gradZ) {
    const double length =
        std::sqrt(1.0 + static_cast<double>(gradX) * gradX + static_cast<double>(gradZ) * gradZ);
    return static_cast<float>(-gradZ / length);
}

ClimateField::ClimateField(std::uint64_t seed, const ClimateParams& params)
    : params_(params), temperatureSeed_(noise::deriveSeed(seed, kTemperature)),
      humiditySeed_(noise::deriveSeed(seed, kHumidity)) {}

float ClimateField::baseTemperatureAt(double x, double z) const {
    const noise::FbmParams fbm{.octaves = 4, .frequency = 1.0 / params_.temperatureWavelength};
    return params_.meanTemperature +
           params_.temperatureAmplitude * noise::fbm2D(x, z, temperatureSeed_, fbm);
}

float ClimateField::humidityAt(double x, double z, float continentalness) const {
    const noise::FbmParams fbm{.octaves = 4, .frequency = 1.0 / params_.humidityWavelength};
    const float inland = noise::smoothstep(params_.inlandLow, params_.inlandHigh, continentalness);
    const float humidity = params_.meanHumidity +
                           params_.humidityAmplitude * noise::fbm2D(x, z, humiditySeed_, fbm) -
                           params_.inlandDrying * inland;
    return std::clamp(humidity, 0.0f, 1.0f);
}

float ClimateField::temperatureAt(float baseTemperature, float altitude, float exposure) const {
    return baseTemperature - params_.lapseRate * std::max(altitude - kSeaLevel, 0.0f) +
           params_.exposureWarming * exposure;
}

ClimateSample ClimateField::sample(double x, double z, const SurfaceSite& site) const {
    ClimateSample climate;
    climate.baseTemperature = baseTemperatureAt(x, z);
    climate.humidity = humidityAt(x, z, site.continentalness);
    climate.slope = slopeOf(site.gradX, site.gradZ);
    climate.exposure = exposureOf(site.gradX, site.gradZ);
    const float warming = site.dry() ? climate.exposure : 0.0f;
    climate.temperature = temperatureAt(climate.baseTemperature, site.waterLevel, warming);
    return climate;
}

} // namespace terrain
