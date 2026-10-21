#include "terrain/io/cliffs_png.hpp"

#include "terrain/det_math.hpp"
#include "terrain/io/eroded_png.hpp"
#include "terrain/io/heightmap_png.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace terrain {

namespace {

// River colors: from small streams to rivers draining 2^6 times the minimum catchment
// (the same scheme as the river map).
constexpr float kStreamRgb[3] = {120.0f, 200.0f, 255.0f};
constexpr float kRiverRgb[3] = {20.0f, 70.0f, 220.0f};
constexpr double kRiverColorDoublings = 6.0;

// A wall is a drop of at least this share of a stratum to a neighbor sample.
constexpr float kWallDropShare = 0.5f;
// A gorge floor lies at least this far (meters) below the eroded relief.
constexpr float kGorgeMinDepth = 2.0f;
// Smallest radius, in pixels, of the ring drawn around a cirque axis.
constexpr double kMinRingRadius = 2.0;

void setPixel(Image& image, int column, int row, const std::uint8_t (&rgb)[3]) {
    const auto offset = static_cast<std::size_t>((row * image.width + column) * 3);
    std::copy(rgb, rgb + 3, image.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
}

void drawRiver(Image& image, int column, int row, double catchment, double minimum) {
    const double doublings = dmath::log2(std::max(catchment / minimum, 1.0));
    const auto t = static_cast<float>(std::min(doublings / kRiverColorDoublings, 1.0));
    std::uint8_t rgb[3];
    for (int c = 0; c < 3; ++c) {
        const float value = kStreamRgb[c] + (kRiverRgb[c] - kStreamRgb[c]) * t;
        rgb[c] = static_cast<std::uint8_t>(std::lround(value));
    }
    setPixel(image, column, row, rgb);
}

// Smallest catchment among the river cells: the river threshold of the network.
double minimumRiverCatchment(const RiverNetwork& rivers) {
    double minimum = 0.0;
    for (std::size_t i = 0; i < rivers.river.size(); ++i) {
        if (rivers.river[i] != 0 && (minimum == 0.0 || rivers.catchment[i] < minimum)) {
            minimum = rivers.catchment[i];
        }
    }
    return minimum;
}

// True if the surface at (column, row) is at least drop meters above a 4-neighbor.
bool dropsToNeighbor(const HeightGrid& surface, int column, int row, float drop) {
    const float height = surface.at(column, row);
    const int last = surface.size - 1;
    return (column > 0 && height - surface.at(column - 1, row) >= drop) ||
           (column < last && height - surface.at(column + 1, row) >= drop) ||
           (row > 0 && height - surface.at(column, row - 1) >= drop) ||
           (row < last && height - surface.at(column, row + 1) >= drop);
}

void drawWalls(Image& image, const CliffRegion& region) {
    const HeightGrid& surface = region.surface();
    const float drop = kWallDropShare * region.field().params().stratumHeight;
    for (int row = 0; row < surface.size; ++row) {
        for (int column = 0; column < surface.size; ++column) {
            if (surface.at(column, row) >= kSeaLevel &&
                dropsToNeighbor(surface, column, row, drop)) {
                setPixel(image, column, row, kCliffWallRgb);
            }
        }
    }
}

void drawGorgeFloors(Image& image, const CliffRegion& region) {
    const HeightGrid& surface = region.surface();
    const HeightGrid& eroded = region.eroded().grid();
    for (int row = 0; row < surface.size; ++row) {
        for (int column = 0; column < surface.size; ++column) {
            const std::size_t i = surface.index(column, row);
            const float halfWidth = region.gorgeHalfWidth()[i];
            if (halfWidth > 0.0f && region.riverDistance()[i] < halfWidth &&
                region.mask()[i] > 0.0f &&
                eroded.heights[i] - surface.heights[i] >= kGorgeMinDepth) {
                setPixel(image, column, row, kGorgeRgb);
            }
        }
    }
}

void drawOceans(Image& image, const HeightGrid& eroded) {
    for (int row = 0; row < eroded.size; ++row) {
        for (int column = 0; column < eroded.size; ++column) {
            if (eroded.at(column, row) < kSeaLevel) {
                setPixel(image, column, row, kOceanRgb);
            }
        }
    }
}

void drawRivers(Image& image, const RiverNetwork& rivers) {
    const double minimum = minimumRiverCatchment(rivers);
    for (int index = 0; index < static_cast<int>(rivers.river.size()); ++index) {
        if (rivers.isRiver(index)) {
            drawRiver(image, index % rivers.size, index / rivers.size,
                      rivers.catchment[static_cast<std::size_t>(index)], minimum);
        }
    }
}

// 1-pixel ring of the given radius around the (fractional) pixel position, clipped to the image.
void drawRing(Image& image, double centerColumn, double centerRow, double radius) {
    const int last = image.width - 1;
    const int firstRow = std::max(static_cast<int>(std::floor(centerRow - radius - 1.0)), 0);
    const int lastRow = std::min(static_cast<int>(std::ceil(centerRow + radius + 1.0)), last);
    const int firstColumn = std::max(static_cast<int>(std::floor(centerColumn - radius - 1.0)), 0);
    const int lastColumn = std::min(static_cast<int>(std::ceil(centerColumn + radius + 1.0)), last);
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const double distance = dmath::hypot(column - centerColumn, row - centerRow);
            if (std::abs(distance - radius) < 0.5) {
                setPixel(image, column, row, kCirqueRgb);
            }
        }
    }
}

void drawCirques(Image& image, const CliffRegion& region) {
    const HeightmapRegion& area = region.eroded().region();
    const double metersPerPixel = area.metersPerPixel;
    const double radius = std::max(
        kMinRingRadius, static_cast<double>(region.field().params().cirqueRadius) / metersPerPixel);
    for (const Cirque& cirque : region.cirques()) {
        // Inverse of ErodedRegion::sampleX and sampleZ.
        const double column = (cirque.x - area.centerX) / metersPerPixel + area.size / 2.0 - 0.5;
        const double row = (cirque.z - area.centerZ) / metersPerPixel + area.size / 2.0 - 0.5;
        drawRing(image, column, row, radius);
    }
}

void drawCascades(Image& image, const RiverNetwork& rivers) {
    for (const Cascade& cascade : rivers.cascades) {
        for (int row = cascade.row - 1; row <= cascade.row + 1; ++row) {
            for (int column = cascade.column - 1; column <= cascade.column + 1; ++column) {
                if (column >= 0 && row >= 0 && column < image.width && row < image.height) {
                    setPixel(image, column, row, kCascadeRgb);
                }
            }
        }
    }
}

} // namespace

Image renderCliffMask(const CliffRegion& region) {
    const int size = region.surface().size;
    Image image{size, size, 1, {}};
    image.pixels.reserve(region.mask().size());
    for (const float mask : region.mask()) {
        const long level = std::lround(255.0f * std::clamp(mask, 0.0f, 1.0f));
        image.pixels.push_back(static_cast<std::uint8_t>(level));
    }
    return image;
}

Image renderCliffPreview(const CliffRegion& region) {
    // Later overlays win: walls, gorge floors, oceans, rivers, cirques, then cascades.
    Image image = renderHeightmapPreview(region.surface());
    drawWalls(image, region);
    drawGorgeFloors(image, region);
    drawOceans(image, region.eroded().grid());
    drawRivers(image, region.eroded().rivers());
    drawCirques(image, region);
    drawCascades(image, region.eroded().rivers());
    return image;
}

bool exportCliffPngs(const CliffRegion& region, const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    const bool mask = writePng(directory / "cliffs_mask.png", renderCliffMask(region));
    const bool preview = writePng(directory / "cliffs_preview.png", renderCliffPreview(region));
    return mask && preview;
}

} // namespace terrain
