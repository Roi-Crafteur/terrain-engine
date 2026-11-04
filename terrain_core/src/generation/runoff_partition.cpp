#include "terrain/generation/runoff_partition.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>

namespace terrain {

namespace {

constexpr std::uint64_t kLithologyStream = 0x11C0;

float runoffStep(float edge0, float edge1, float v) {
    return noise::smoothstep(edge0, edge1, v);
}

float runoffMix(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace

RunoffShare partitionRunoff(const RunoffSite& site, const RunoffParams& params) {
    RunoffShare share;
    const float pet = std::max(site.pet, 300.0f);
    const float precipitation = std::max(site.precipitation, 0.0f);
    const float humidity = precipitation / (precipitation + pet);
    // The forest of a wet, mild climate; the bare ground of the deserts and the tundra.
    share.vegetation =
        runoffStep(0.3f, 0.6f, humidity) * runoffStep(-6.0f, 4.0f, site.temperature);
    // The convective rains of the tropics, the rare violent rains of the deserts.
    const float tropical = 1.0f - runoffStep(18.0f, 32.0f, site.latitude);
    const float arid = 1.0f - runoffStep(0.15f, 0.35f, humidity);
    share.storm = std::clamp(0.35f + 0.45f * tropical + 0.4f * arid, 0.0f, 1.0f);
    share.frost = 1.0f - runoffStep(-7.0f, -1.0f, site.temperature);
    // What the ground takes in: its permeability, more under the plants, nothing frozen.
    const float takes = std::clamp(site.permeability, 0.0f, 1.0f) *
                        (0.45f + 0.55f * share.vegetation) * (1.0f - share.frost);
    const float slope = std::max(site.slope, 0.0f);
    const float steep = slope / (slope + params.slopeHalf);
    share.surface = std::clamp(params.leastSurface + (1.0f - params.leastSurface) * share.storm *
                                                         (1.0f - takes) * (0.35f + 0.65f * steep),
                               0.0f, 1.0f);
    // The channel heads: area x slope x surface water of a year.
    const double water =
        static_cast<double>(share.surface) *
        std::max(static_cast<double>(precipitation), params.stormRain) * 1.0e-3; // m/yr
    const double drop = std::max(static_cast<double>(slope), params.headSlope);
    const double dry =
        1.0 - static_cast<double>(runoffStep(params.aridHumidity, params.wetHumidity, humidity));
    const double most = params.mostHead +
                        static_cast<double>(site.permeability) * params.permeableHead +
                        dry * params.aridHead;
    share.headArea = std::clamp(params.headFlux / (water * drop), params.leastHead, most);
    return share;
}

bool channelWet(double flow, float surface, const RunoffParams& params) {
    const double base = flow * (1.0 - static_cast<double>(std::clamp(surface, 0.0f, 1.0f)));
    return base >= params.wetFlow || flow >= params.wetTotal;
}

Permeability::Permeability(std::uint64_t seed, const WorldSize& world, const RunoffParams& params)
    : params_(params),
      lithology_(noise::deriveSeed(seed, kLithologyStream),
                 {.octaves = 3, .frequency = 1.0 / params.lithologyWavelength}, world) {}

float Permeability::lithology(double x, double z) const {
    return std::clamp(1.6f * lithology_.fbm2D(x, z), -1.0f, 1.0f);
}

float Permeability::at(double x, double z, const HeightSample& s, float character) const {
    const RunoffParams& p = params_;
    // The sediments of the plains and basins, from clays to limestones.
    float k = runoffMix(p.clay, p.limestone, runoffStep(-0.6f, 0.6f, lithology(x, z)));
    // The hard rock under the mountains and the hills: young and sharp, crystalline; worn, old
    // massifs a little more open.
    const float hard = runoffMix(p.oldMassif, p.crystalline, std::clamp(character, 0.0f, 1.0f));
    const float mountain = runoffStep(0.05f, 0.4f, s.mountainMask);
    const float hills = 0.5f * runoffStep(0.3f, 0.7f, s.relief) * (1.0f - mountain);
    k = runoffMix(k, hard, std::max(mountain, hills));
    k = runoffMix(k, p.riftFill, std::clamp(s.rift, 0.0f, 1.0f));
    k = runoffMix(k, p.sand, std::clamp(s.erg, 0.0f, 1.0f));
    return std::clamp(k, 0.0f, 1.0f);
}

} // namespace terrain
