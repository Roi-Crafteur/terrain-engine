#include "terrain/io/blend_png.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace terrain {

namespace {

template <std::size_t N>
Rgb8 mix(const std::array<float, N>& weights, const std::array<Rgb8, N>& colors) {
    std::array<double, 3> sum{};
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t c = 0; c < sum.size(); ++c) {
            sum[c] += static_cast<double>(weights[i]) * colors[i][c];
        }
    }
    Rgb8 rgb{};
    for (std::size_t c = 0; c < rgb.size(); ++c) {
        rgb[c] = static_cast<std::uint8_t>(std::lround(std::clamp(sum[c], 0.0, 255.0)));
    }
    return rgb;
}

template <class Color>
Image render(std::span<const BlendWeights> weights, int width, int height, Color&& color) {
    if (width <= 0 || height <= 0 ||
        weights.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        throw std::invalid_argument("blend map: the weights do not fit the size");
    }
    Image image{width, height, 3, {}};
    image.pixels.reserve(weights.size() * 3);
    for (const BlendWeights& w : weights) {
        const Rgb8 rgb = color(w);
        image.pixels.insert(image.pixels.end(), rgb.begin(), rgb.end());
    }
    return image;
}

} // namespace

BlendWeights dominantWeights(const SurfaceBlend& blend) {
    BlendWeights weights;
    weights.biomes[static_cast<std::size_t>(blend.biomes[0])] = 1.0f;
    weights.materials[static_cast<std::size_t>(blend.materials[0])] = 1.0f;
    return weights;
}

BlendWeights blendWeights(const SurfaceBlend& blend) {
    BlendWeights weights;
    addBlend(weights, blend, 1.0f);
    return weights;
}

Rgb8 mixedBiomeColor(const BlendWeights& weights) {
    return mix(weights.biomes, kBiomeRgb);
}

Rgb8 mixedMaterialColor(const BlendWeights& weights) {
    return mix(weights.materials, kMaterialRgb);
}

Image renderMixedBiomeMap(std::span<const BlendWeights> weights, int width, int height) {
    return render(weights, width, height, mixedBiomeColor);
}

Image renderMixedMaterialMap(std::span<const BlendWeights> weights, int width, int height) {
    return render(weights, width, height, mixedMaterialColor);
}

} // namespace terrain
