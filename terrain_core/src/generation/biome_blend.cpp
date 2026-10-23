#include "terrain/generation/biome_blend.hpp"

#include "biome_margins.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace terrain {

namespace {

constexpr float kWeightUnits = 255.0f;

bool positive(double value) {
    return value > 0.0 && std::isfinite(value);
}

// The 3 dominant ids of a set of margins, the dominant one given, and their weights. The raw
// weight of an id is 1 - smoothstep of its margin times factor: 1 inside its region (the
// dominant one), 0 a width beyond it. The 4th largest raw weight is taken off the 3 others before
// they are normalized, so that a weight reaches 0 before its id leaves the 3: continuous when
// the 3rd and the 4th swap.
template <std::size_t N, class Id>
void dominants(const std::array<float, N>& margins, Id dominant, float factor,
               std::array<Id, kBlendSlots>& ids, std::array<float, kBlendSlots>& weights) {
    const auto first = static_cast<std::size_t>(dominant);
    const auto raw = [&margins, factor](std::size_t i) {
        return 1.0f - noise::smoothstep(0.0f, 1.0f, margins[i] * factor);
    };
    std::array<std::size_t, kBlendSlots> slots = {first, first, first};
    std::array<float, kBlendSlots + 1> g = {raw(first), 0.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < N; ++i) {
        const float v = i == first ? 0.0f : raw(i);
        if (v > g[1]) {
            g[3] = g[2];
            g[2] = g[1];
            slots[2] = slots[1];
            g[1] = v;
            slots[1] = i;
        } else if (v > g[2]) {
            g[3] = g[2];
            g[2] = v;
            slots[2] = i;
        } else if (v > g[3]) {
            g[3] = v;
        }
    }
    const float sum = (g[0] - g[3]) + (g[1] - g[3]) + (g[2] - g[3]);
    weights = {1.0f, 0.0f, 0.0f};
    if (sum > 0.0f) {
        // One divisor for all: the weights keep the order of the raw weights.
        for (std::size_t s = 0; s < kBlendSlots; ++s) {
            weights[s] = (g[s] - g[3]) / sum;
        }
    }
    for (std::size_t s = 0; s < kBlendSlots; ++s) {
        ids[s] = static_cast<Id>(weights[s] > 0.0f ? slots[s] : first);
    }
}

// Weights of slots 1 and 2 in 255ths, and slot 0 the rest, never below slot 1.
std::array<std::uint32_t, kBlendSlots> quantize(const std::array<float, kBlendSlots>& w) {
    const auto units = [](float weight) {
        return static_cast<std::uint32_t>(std::clamp(weight, 0.0f, 1.0f) * kWeightUnits + 0.5f);
    };
    const std::uint32_t q2 = units(w[2]);
    std::uint32_t q1 = std::min(units(w[1]), 255U - q2);
    if (255U - q1 - q2 < q1) {
        q1 = (255U - q2) / 2U;
    }
    return {255U - q1 - q2, q1, q2};
}

std::uint32_t field(std::uint32_t word, int shift, std::uint32_t mask) {
    return (word >> shift) & mask;
}

} // namespace

void validate(const BlendParams& params) {
    if (!positive(params.temperatureWidth) || !positive(params.humidityWidth) ||
        !positive(params.relativeWidth)) {
        throw std::invalid_argument("BlendParams: the widths must be positive and finite");
    }
    if (!positive(params.noiseWavelength) || params.noiseOctaves < 1 || params.noiseOctaves > 16) {
        throw std::invalid_argument("BlendParams: invalid border noise");
    }
    if (!(params.noiseAmplitude >= 0.0f && params.noiseAmplitude < 1.0f)) {
        throw std::invalid_argument("BlendParams: the noise amplitude must lie in [0, 1)");
    }
}

SurfaceBlend blendSurface(const ClimateSample& climate, const SurfaceSite& site,
                          const BiomeParams& biomes, const MaterialParams& materials,
                          const BlendParams& params, float widthFactor) {
    const Biome biome = classifyBiome(climate, site, biomes);
    const SurfaceMaterial material = surfaceMaterial(biome, climate, site, materials);
    const blend::Margins margins = blend::regionMargins(climate, site, biomes, materials, params);
    SurfaceBlend result;
    dominants(margins.biomes, biome, widthFactor, result.biomes, result.biomeWeights);
    dominants(margins.materials, material, widthFactor, result.materials, result.materialWeights);
    return result;
}

BlendField::BlendField(std::uint64_t seed, const BlendParams& params, const WorldSize& world)
    : params_(params), world_(world), seed_(seed) {
    validate(params_);
    validate(world_);
}

float BlendField::widthFactor(double x, double z) const {
    const noise::FbmParams fbm{.octaves = params_.noiseOctaves,
                               .frequency = 1.0 / params_.noiseWavelength};
    return 1.0f + params_.noiseAmplitude * noise::fbm2D(x, z, seed_, fbm, world_);
}

SurfaceBlend BlendField::blend(double x, double z, const ClimateSample& climate,
                               const SurfaceSite& site, const BiomeParams& biomes,
                               const MaterialParams& materials) const {
    return blendSurface(climate, site, biomes, materials, params_, widthFactor(x, z));
}

BlendTexel packBlend(const SurfaceBlend& blend) {
    const std::array<std::uint32_t, kBlendSlots> biomes = quantize(blend.biomeWeights);
    const std::array<std::uint32_t, kBlendSlots> materials = quantize(blend.materialWeights);
    BlendTexel texel;
    for (std::size_t s = 0; s < kBlendSlots; ++s) {
        // A slot rounded to 0 repeats the id of slot 0, as in SurfaceBlend.
        const Biome biome = biomes[s] > 0U ? blend.biomes[s] : blend.biomes[0];
        const SurfaceMaterial material =
            materials[s] > 0U ? blend.materials[s] : blend.materials[0];
        const int shift = kBlendIdBits * static_cast<int>(s);
        texel.ids |= static_cast<std::uint32_t>(biome) << shift;
        texel.ids |= static_cast<std::uint32_t>(material) << (shift + kBlendIdBits * kBlendSlots);
    }
    texel.weights = biomes[0] | (biomes[1] << 8) | (materials[0] << 16) | (materials[1] << 24);
    return texel;
}

SurfaceBlend unpackBlend(BlendTexel texel) {
    constexpr std::uint32_t kIdMask = (1U << kBlendIdBits) - 1U;
    SurfaceBlend blend;
    for (std::size_t s = 0; s < kBlendSlots; ++s) {
        const int shift = kBlendIdBits * static_cast<int>(s);
        blend.biomes[s] = static_cast<Biome>(field(texel.ids, shift, kIdMask));
        blend.materials[s] = static_cast<SurfaceMaterial>(
            field(texel.ids, shift + kBlendIdBits * kBlendSlots, kIdMask));
    }
    const std::uint32_t b0 = field(texel.weights, 0, 0xFFU);
    const std::uint32_t b1 = field(texel.weights, 8, 0xFFU);
    const std::uint32_t m0 = field(texel.weights, 16, 0xFFU);
    const std::uint32_t m1 = field(texel.weights, 24, 0xFFU);
    const auto share = [](std::uint32_t units) { return static_cast<float>(units) / kWeightUnits; };
    const auto rest = [](std::uint32_t a, std::uint32_t b) {
        return a + b <= 255U ? 255U - a - b : 0U;
    };
    blend.biomeWeights = {share(b0), share(b1), share(rest(b0, b1))};
    blend.materialWeights = {share(m0), share(m1), share(rest(m0, m1))};
    return blend;
}

void addBlend(BlendWeights& weights, const SurfaceBlend& blend, float share) {
    for (std::size_t s = 0; s < kBlendSlots; ++s) {
        weights.biomes[static_cast<std::size_t>(blend.biomes[s])] += share * blend.biomeWeights[s];
        weights.materials[static_cast<std::size_t>(blend.materials[s])] +=
            share * blend.materialWeights[s];
    }
}

void addTexel(BlendWeights& weights, BlendTexel texel, float share) {
    addBlend(weights, unpackBlend(texel), share);
}

} // namespace terrain
