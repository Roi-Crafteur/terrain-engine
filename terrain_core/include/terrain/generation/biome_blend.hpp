#pragma once

#include "terrain/generation/biome.hpp"
#include "terrain/generation/climate.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>

namespace terrain {

// Transitions between the biomes and between the surface materials: the weights of the 2 or 3
// dominant biomes and materials of a column, for the renderers to blend the textures and the
// density of the vegetation. They are added to the classification, which they never change:
// the dominant biome is classifyBiome and the dominant material surfaceMaterial, with the same
// bits, and no height ever depends on them.
//
// Every threshold of classifyBiome and surfaceMaterial gets a width: the weight of the biome
// across it falls from one half on the threshold to 0 one width beyond it, on either side
// (smoothstep). Climate thresholds (the Whittaker diagram, the snow, the tree line) are wide on
// the plains, where the climate changes slowly (a few km between forest and grassland), and
// narrow on the slopes, where the temperature follows the altitude; ground thresholds (the
// height of a beach, the slope of rock) change within a few meters. The water (the ocean and
// the rivers), the vertical walls of the cliffs and the rock above air stay sharp: they are
// shapes of the relief.
//
// A periodic noise (BlendField) widens or narrows the bands from place to place: tongues of the
// neighbor biome, groves and clearings where it rises up to nearly one half. The line where two
// biomes weigh one half each stays the border of the classification.

inline constexpr int kBlendSlots = 3;

// Widths of the transitions, each on both sides of its threshold.
struct BlendParams {
    // Temperatures (degrees Celsius): snow, tree line, Whittaker diagram, marsh, cold beaches
    // and mountain snow.
    float temperatureWidth = 0.6f;
    // Humidities (in [0, 1]): Whittaker diagram and marsh.
    float humidityWidth = 0.04f;
    // Altitudes, slopes and distances: this share of the threshold (4 m beaches: 0.8 m; rock
    // from a slope of 1: 0.2).
    float relativeWidth = 0.2f;

    // Border noise: the margins are multiplied by 1 + noiseAmplitude * fbm, an fbm of
    // noiseOctaves octaves whose first wavelength is noiseWavelength meters. noiseAmplitude in
    // [0, 1): 0 gives bands of constant width.
    double noiseWavelength = 3000.0;
    int noiseOctaves = 4;
    float noiseAmplitude = 0.8f;
};

// Throws std::invalid_argument unless the widths are positive and finite, the wavelength is
// positive and finite, the octaves are in [1, 16] and the amplitude is in [0, 1).
void validate(const BlendParams& params);

// The dominant biomes and materials of a column. Slot 0 holds the dominant one (classifyBiome,
// surfaceMaterial); the weights decrease from slot to slot, lie in [0, 1] and sum to 1; a slot of
// weight 0 repeats the id of slot 0.
struct SurfaceBlend {
    std::array<Biome, kBlendSlots> biomes{};
    std::array<float, kBlendSlots> biomeWeights{};
    std::array<SurfaceMaterial, kBlendSlots> materials{};
    std::array<float, kBlendSlots> materialWeights{};

    friend bool operator==(const SurfaceBlend&, const SurfaceBlend&) = default;
};

// The weights of a column of the given climate and site. widthFactor (> 0) multiplies every
// margin: above 1 the bands narrow, below 1 they widen (BlendField::widthFactor). Pure function:
// continuous in its inputs away from the sharp borders; it never changes a height.
[[nodiscard]] SurfaceBlend blendSurface(const ClimateSample& climate, const SurfaceSite& site,
                                        const BiomeParams& biomes, const MaterialParams& materials,
                                        const BlendParams& params, float widthFactor = 1.0f);

// Deterministic border noise of the transitions: the same seed, parameters and position always
// give the same factor. Periodic on the world (WorldSize), as the climate.
class BlendField {
public:
    // Throws std::invalid_argument for invalid parameters or an invalid world (validate).
    BlendField(std::uint64_t seed, const BlendParams& params = {}, const WorldSize& world = {});

    // 1 + noiseAmplitude * fbm at (x, z), in [1 - noiseAmplitude, 1 + noiseAmplitude].
    [[nodiscard]] float widthFactor(double x, double z) const;
    [[nodiscard]] SurfaceBlend blend(double x, double z, const ClimateSample& climate,
                                     const SurfaceSite& site, const BiomeParams& biomes,
                                     const MaterialParams& materials) const;

    [[nodiscard]] const BlendParams& params() const { return params_; }
    [[nodiscard]] const WorldSize& world() const { return world_; }

private:
    BlendParams params_;
    WorldSize world_;
    std::uint64_t seed_;
};

// Compact form of a SurfaceBlend, 8 bytes: two 32-bit words, each an RGBA8 texel (R the lowest
// byte) or one texel of two 32-bit channels.
// - ids: biome of slot 0 in bits 0-3, of slot 1 in bits 4-7, of slot 2 in bits 8-11; material
//   of slot 0 in bits 12-15, slot 1 in bits 16-19, slot 2 in bits 20-23; bits 24-31 are 0.
// - weights: biome weights of slots 0 and 1 in bits 0-7 and 8-15, material weights of slots 0
//   and 1 in bits 16-23 and 24-31, in 255ths; the weight of slot 2 is 255 minus the two others.
// A reader blending texels (bilinear between 4 of them, say) adds the weight of each slot to its
// id, never interpolating the ids themselves (addTexel is the reference).
struct BlendTexel {
    std::uint32_t ids = 0;
    std::uint32_t weights = 0;

    friend bool operator==(const BlendTexel&, const BlendTexel&) = default;
};
static_assert(sizeof(BlendTexel) == 8, "a blend texel holds 8 bytes");

// Bits of an id in a texel. Ids that no longer fit need wider fields, never truncated ones.
inline constexpr int kBlendIdBits = 4;
static_assert(kBiomeCount <= (1 << kBlendIdBits), "a biome id fits in 4 bits");
static_assert(kSurfaceMaterialCount <= (1 << kBlendIdBits), "a material id fits in 4 bits");

// The weights rounded to 255ths (slot 0 takes what the rounding of the others leaves, never
// less than slot 1): they sum to exactly 255.
[[nodiscard]] BlendTexel packBlend(const SurfaceBlend& blend);
// The blend of a texel, its weights in 255ths.
[[nodiscard]] SurfaceBlend unpackBlend(BlendTexel texel);

// Weight of every biome and every material.
struct BlendWeights {
    std::array<float, kBiomeCount> biomes{};
    std::array<float, kSurfaceMaterialCount> materials{};
};

// Adds share times the weights of the blend to each id.
void addBlend(BlendWeights& weights, const SurfaceBlend& blend, float share);
// Adds share times the weights of the texel to each id: share 1 gives the weights of the texel,
// the bilinear shares of 4 texels their bilinear blend.
void addTexel(BlendWeights& weights, BlendTexel texel, float share);

} // namespace terrain
