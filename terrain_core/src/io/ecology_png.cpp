#include "terrain/io/ecology_png.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace terrain {

namespace {

Image blankImage(const EcologyMap& map) {
    Image image{map.size(), map.size(), 3, {}};
    image.pixels.reserve(map.samples.size() * 3);
    return image;
}

void append(Image& image, const Rgb8& rgb) {
    image.pixels.insert(image.pixels.end(), rgb.begin(), rgb.end());
}

Rgb8 ramp(const Rgb8& low, const Rgb8& high, float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    Rgb8 rgb{};
    for (std::size_t c = 0; c < rgb.size(); ++c) {
        const auto from = static_cast<float>(low[c]);
        const float value = from + (static_cast<float>(high[c]) - from) * clamped;
        rgb[c] = static_cast<std::uint8_t>(std::lround(value));
    }
    return rgb;
}

} // namespace

Image renderBiomeMap(const EcologyMap& map) {
    Image image = blankImage(map);
    for (const EcologySample& sample : map.samples) {
        append(image, kBiomeRgb[static_cast<std::size_t>(sample.biome)]);
    }
    return image;
}

Image renderSurfaceMaterialMap(const EcologyMap& map) {
    Image image = blankImage(map);
    for (const EcologySample& sample : map.samples) {
        append(image, kMaterialRgb[static_cast<std::size_t>(sample.material)]);
    }
    return image;
}

Image renderVegetationDensityMap(const EcologyMap& map) {
    Image image = blankImage(map);
    for (const EcologySample& sample : map.samples) {
        const float density = totalDensity(sample.densities);
        if (sample.material == SurfaceMaterial::Water) {
            append(image, ramp(kOpenWaterRgb, kDenseSeaweedRgb, density));
        } else {
            append(image, ramp(kBareGroundRgb, kDenseVegetationRgb, density));
        }
    }
    return image;
}

bool exportEcologyPngs(const EcologyMap& map, const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    const bool biomes = writePng(directory / "biome_map.png", renderBiomeMap(map));
    const bool materials =
        writePng(directory / "surface_material_map.png", renderSurfaceMaterialMap(map));
    const bool vegetation =
        writePng(directory / "vegetation_density_map.png", renderVegetationDensityMap(map));
    return biomes && materials && vegetation;
}

} // namespace terrain
