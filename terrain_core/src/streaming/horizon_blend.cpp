#include "horizon_blend.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace terrain::horizon {

namespace {

// The kBlendSlots heaviest ids of weights (the lower id first between equal weights): each id
// inserted after the slots at least as heavy, the lighter ones shifted down.
template <std::size_t N>
std::array<int, kBlendSlots> heaviest(const std::array<float, N>& weights) {
    std::array<int, kBlendSlots> top{};
    std::array<float, kBlendSlots> best{};
    best.fill(-1.0f);
    for (std::size_t id = 0; id < N; ++id) {
        const float weight = weights[id];
        std::size_t slot = 0;
        while (slot < kBlendSlots && best[slot] >= weight) {
            ++slot;
        }
        for (std::size_t last = kBlendSlots - 1; slot < kBlendSlots && last > slot; --last) {
            best[last] = best[last - 1];
            top[last] = top[last - 1];
        }
        if (slot < kBlendSlots) {
            best[slot] = weight;
            top[slot] = static_cast<int>(id);
        }
    }
    return top;
}

// The ids and weights of the dominant slots of weights, as blendSurface leaves them.
template <typename Id, std::size_t N>
void dominant(const std::array<float, N>& weights, std::array<Id, kBlendSlots>& ids,
              std::array<float, kBlendSlots>& shares) {
    const std::array<int, kBlendSlots> top = heaviest(weights);
    float sum = 0.0f;
    for (const int id : top) {
        sum += weights[static_cast<std::size_t>(id)];
    }
    for (std::size_t slot = 0; slot < kBlendSlots; ++slot) {
        const float weight = weights[static_cast<std::size_t>(top[slot])];
        shares[slot] = sum > 0.0f ? weight / sum : (slot == 0 ? 1.0f : 0.0f);
        ids[slot] = static_cast<Id>(shares[slot] > 0.0f ? top[slot] : top[0]);
    }
}

} // namespace

SurfaceSite pointSite(const HeightSample& sample, const std::array<float, 4>& h, double step) {
    SurfaceSite site;
    site.ground = sample.height;
    site.continentalness = sample.continentalness;
    const double run = 2.0 * step;
    site.gradX = static_cast<float>((static_cast<double>(h[0]) - h[1]) / run);
    site.gradZ = static_cast<float>((static_cast<double>(h[2]) - h[3]) / run);
    float difference = 0.0f;
    for (const float height : h) {
        difference = std::max(difference, std::abs(height - site.ground));
    }
    site.steepness = static_cast<float>(difference / step);
    site.water = sample.water;
    // The top of the column (SurfaceSite::waterLevel): the ground on dry land, never the level a
    // river nearby leaves in HeightSample::waterLevel there (agent 49: the climate of a dry sample
    // at its ground, as the texels of the tiles and the map read it).
    site.waterLevel = sample.water == WaterKind::None ? sample.height : sample.waterLevel;
    site.dunes = sample.dunes;
    site.erg = sample.erg;
    site.ergFloor = sample.ergFloor;
    site.volcanicGround = sample.volcanicGround;
    site.volcanicShare = static_cast<float>(sample.volcanicShare) / 255.0f;
    return site;
}

SurfaceBlend dominantBlend(const BlendWeights& weights) {
    SurfaceBlend blend;
    dominant(weights.biomes, blend.biomes, blend.biomeWeights);
    dominant(weights.materials, blend.materials, blend.materialWeights);
    // The share of the dunes: their mean over the weights summed (those of the materials).
    float total = 0.0f;
    for (const float weight : weights.materials) {
        total += weight;
    }
    blend.dunes = total > 0.0f ? std::clamp(weights.dunes / total, 0.0f, 1.0f) : 0.0f;
    return blend;
}

SurfaceBlend footprintBlend(const std::array<const SurfaceBlend*, 9>& around) {
    BlendWeights sum;
    for (std::size_t n = 0; n < around.size(); ++n) {
        addBlend(sum, *around[n], kFootprintShares[n]);
    }
    return dominantBlend(sum);
}

BlendTexel withMaterial(BlendTexel texel, SurfaceMaterial material) {
    const auto id = static_cast<std::uint32_t>(material);
    // The same material in the 3 slots; no share of the dunes (the edits cover their sand).
    texel.materials = id | id << kMaterialIdBits | id << (2 * kMaterialIdBits);
    // All the weight on slot 0; the bits kept for the heat of the ground (16-23) unchanged.
    texel.materialWeights = (texel.materialWeights & 0x00FF0000U) | 255U;
    return texel;
}

} // namespace terrain::horizon
