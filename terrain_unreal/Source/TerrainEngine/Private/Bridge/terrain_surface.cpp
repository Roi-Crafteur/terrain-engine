#include "terrain_surface.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::unreal {

float packSurface(SurfaceMaterial material, Biome biome) {
    return static_cast<float>(static_cast<int>(material) +
                              kSurfaceBiomeStride * static_cast<int>(biome));
}

SurfaceCode unpackSurface(float code) {
    if (!(code >= 0.0f) || code >= static_cast<float>(kSurfaceBiomeStride * kBiomeCount)) {
        return {};
    }
    const int value = static_cast<int>(code);
    const int material = value % kSurfaceBiomeStride;
    if (material >= kSurfaceMaterialCount) {
        return {};
    }
    return {static_cast<SurfaceMaterial>(material),
            static_cast<Biome>(value / kSurfaceBiomeStride)};
}

DropFade dropFade(const TileHole& hole, double level0PitchMeters) {
    return {hole.halfCells * level0PitchMeters + hole.overlapMeters, hole.overlapMeters / 2.0};
}

double dropShare(const DropFade& fade, double chebyshevMeters) {
    if (!(fade.fadeMeters > 0.0)) {
        return chebyshevMeters < fade.coveredMeters ? 1.0 : 0.0;
    }
    return std::clamp((fade.coveredMeters - chebyshevMeters) / fade.fadeMeters, 0.0, 1.0);
}

} // namespace terrain::unreal
