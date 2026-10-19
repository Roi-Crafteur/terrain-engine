#include "terrain/io/arches_png.hpp"

#include "terrain/io/cliffs_png.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace terrain {

namespace {

void setPixel(Image& image, int column, int row, const std::uint8_t (&rgb)[3]) {
    if (column < 0 || row < 0 || column >= image.width || row >= image.height) {
        return;
    }
    const auto offset = static_cast<std::size_t>((row * image.width + column) * 3);
    std::copy(rgb, rgb + 3, image.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
}

// Position of a world point in pixels (inverse of ErodedRegion::sampleX and sampleZ).
struct PixelPoint {
    double column;
    double row;
};

PixelPoint pixelOf(const HeightmapRegion& area, double x, double z) {
    return {(x - area.centerX) / area.metersPerPixel + area.size / 2.0 - 0.5,
            (z - area.centerZ) / area.metersPerPixel + area.size / 2.0 - 0.5};
}

void setPixel(Image& image, const PixelPoint& point, const std::uint8_t (&rgb)[3]) {
    setPixel(image, static_cast<int>(std::lround(point.column)),
             static_cast<int>(std::lround(point.row)), rgb);
}

void drawOverhangs(Image& image, const SculptedRegion& region) {
    const HeightGrid& surface = region.surface();
    for (int row = 0; row < surface.size; ++row) {
        for (int column = 0; column < surface.size; ++column) {
            if (region.airBelowRock()[surface.index(column, row)] != 0) {
                setPixel(image, column, row, kOverhangRgb);
            }
        }
    }
}

// Segment between the pillars, sampled every half pixel, then the pillars at its ends.
void drawArch(Image& image, const HeightmapRegion& area, const Arch& arch) {
    const PixelPoint center = pixelOf(area, arch.x, arch.z);
    const double half = std::max(kMinArchHalfLength, arch.halfSpan / area.metersPerPixel);
    const int steps = static_cast<int>(std::ceil(4.0 * half));
    for (int i = 0; i <= steps; ++i) {
        const double t = half * (2.0 * i / steps - 1.0);
        setPixel(image, {center.column + t * arch.directionX, center.row + t * arch.directionZ},
                 kArchSpanRgb);
    }
    for (const double side : {-1.0, 1.0}) {
        setPixel(image,
                 {center.column + side * half * arch.directionX,
                  center.row + side * half * arch.directionZ},
                 kArchPillarRgb);
    }
}

} // namespace

Image renderOverhangMask(const SculptedRegion& region) {
    const int size = region.surface().size;
    Image image{size, size, 1, {}};
    image.pixels.reserve(region.controlMask().size());
    for (const float mask : region.controlMask()) {
        const long level = std::lround(255.0f * std::clamp(mask, 0.0f, 1.0f));
        image.pixels.push_back(static_cast<std::uint8_t>(level));
    }
    return image;
}

Image renderArchesPreview(const SculptedRegion& region) {
    Image image = renderCliffPreview(region.cliffs());
    drawOverhangs(image, region);
    for (const Arch& arch : region.arches()) {
        drawArch(image, region.cliffs().eroded().region(), arch);
    }
    return image;
}

bool exportArchPngs(const SculptedRegion& region, const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    const bool mask = writePng(directory / "overhangs_mask.png", renderOverhangMask(region));
    const bool preview = writePng(directory / "arches_preview.png", renderArchesPreview(region));
    return mask && preview;
}

} // namespace terrain
