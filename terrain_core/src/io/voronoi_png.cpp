#include "terrain/io/voronoi_png.hpp"

#include "terrain/generation/noise.hpp"
#include "terrain/io/heightmap_png.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <system_error>
#include <vector>

namespace terrain {

namespace {

constexpr float kBeltTint = 0.6f; // tint of the collision belts and rift troughs at full strength
constexpr PlateColor kUnknownBoundaryColor = {255, 255, 255};

// Calls visit(index, x, z) for every pixel of region, row by row, at the pixel centers of
// sampleHeightGrid.
template <class Visit>
void forEachPixel(const HeightmapRegion& region, Visit visit) {
    const double half = region.size / 2.0;
    std::size_t index = 0;
    for (int row = 0; row < region.size; ++row) {
        const double z = region.centerZ + (row + 0.5 - half) * region.metersPerPixel;
        for (int column = 0; column < region.size; ++column) {
            const double x = region.centerX + (column + 0.5 - half) * region.metersPerPixel;
            visit(index++, x, z);
        }
    }
}

std::size_t pixelCount(const HeightmapRegion& region) {
    return static_cast<std::size_t>(region.size) * static_cast<std::size_t>(region.size);
}

std::uint8_t channel(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

PlateColor mix(PlateColor a, PlateColor b, float t) {
    PlateColor c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = channel(a[i] + (static_cast<float>(b[i]) - a[i]) * t);
    }
    return c;
}

// Brightness factor of a plate, in [0.88, 1.12], hashed from its index.
PlateColor shaded(PlateColor color, int plate) {
    const std::uint64_t h = noise::deriveSeed(0x504C41544553ULL, static_cast<std::uint64_t>(plate));
    const float factor = 0.88f + 0.24f * static_cast<float>(h >> 40) / 16777216.0f; // 2^24
    PlateColor c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = channel(color[i] * factor);
    }
    return c;
}

void put(Image& image, std::size_t pixel, PlateColor color) {
    std::copy(color.begin(), color.end(),
              image.pixels.begin() + static_cast<std::ptrdiff_t>(pixel * 3));
}

PlateColor fillColor(const TectonicPlates& plates, const PlateSample& s) {
    const PlateKind kind = plates.plates()[static_cast<std::size_t>(s.plate)].kind;
    PlateColor color = shaded(plateKindColor(kind), s.plate);
    color = mix(color, kCollisionColor, kBeltTint * s.collision);
    const float depth = plates.params().riftDepth;
    const float trough = depth > 0.0f ? std::clamp(-s.rift / depth, 0.0f, 1.0f) : 0.0f;
    return mix(color, kDivergenceColor, kBeltTint * trough);
}

// Boundary lines: pixels whose right or lower neighbor belongs to another plate, in the color
// of the boundary between them.
void drawBoundaries(const TectonicPlates& plates, const std::vector<std::uint8_t>& plateOf,
                    int size, Image& image) {
    const auto side = static_cast<std::size_t>(size);
    for (std::size_t i = 0; i < plateOf.size(); ++i) {
        int other = -1;
        if (i % side + 1 < side && plateOf[i + 1] != plateOf[i]) {
            other = plateOf[i + 1];
        } else if (i + side < plateOf.size() && plateOf[i + side] != plateOf[i]) {
            other = plateOf[i + side];
        }
        if (other < 0) {
            continue;
        }
        const int index = plates.boundaryIndex(plateOf[i], other);
        put(image, i,
            index >= 0
                ? boundaryKindColor(plates.boundaries()[static_cast<std::size_t>(index)].kind)
                : kUnknownBoundaryColor);
    }
}

// Plate coastline: pixels where the plate continentalness changes sign toward the right or
// lower neighbor.
void drawCoastline(const std::vector<float>& continentalness, int size, Image& image) {
    const auto side = static_cast<std::size_t>(size);
    for (std::size_t i = 0; i < continentalness.size(); ++i) {
        const bool land = continentalness[i] >= 0.0f;
        const bool right = i % side + 1 < side && (continentalness[i + 1] >= 0.0f) != land;
        const bool down =
            i + side < continentalness.size() && (continentalness[i + side] >= 0.0f) != land;
        if (right || down) {
            put(image, i, kPlateCoastColor);
        }
    }
}

} // namespace

PlateColor plateKindColor(PlateKind kind) {
    switch (kind) {
    case PlateKind::Continent:
        return kContinentColor;
    case PlateKind::InlandSea:
        return kInlandSeaColor;
    case PlateKind::ContinentalShelf:
        return kShelfColor;
    case PlateKind::Ocean:
        break;
    }
    return kOceanColor;
}

PlateColor boundaryKindColor(BoundaryKind kind) {
    switch (kind) {
    case BoundaryKind::Collision:
        return kCollisionColor;
    case BoundaryKind::Divergence:
        return kDivergenceColor;
    case BoundaryKind::Transform:
        break;
    }
    return kTransformColor;
}

Image renderPlatesMap(const TectonicPlates& plates, const HeightmapRegion& region) {
    const std::size_t count = pixelCount(region);
    Image image{region.size, region.size, 3, std::vector<std::uint8_t>(count * 3)};
    std::vector<std::uint8_t> plateOf(count);
    std::vector<float> continentalness(count);
    forEachPixel(region, [&](std::size_t i, double x, double z) {
        const PlateSample s = plates.sampleAt(x, z);
        plateOf[i] = static_cast<std::uint8_t>(s.plate);
        continentalness[i] = s.continentalness;
        put(image, i, fillColor(plates, s));
    });
    drawCoastline(continentalness, region.size, image);
    drawBoundaries(plates, plateOf, region.size, image);
    return image;
}

HeightmapRegion planetRegion(const TectonicPlates& plates, int size) {
    const double side = std::max(plates.world().x, plates.world().z);
    return {0.0, 0.0, side / size, size};
}

Image renderWorldMap(const Heightfield& field, int size) {
    const TectonicPlates& plates = *field.plates();
    const HeightmapRegion world = planetRegion(plates, size);
    Image image = renderHeightmapPreview(sampleHeightGrid(field, world));
    std::vector<std::uint8_t> plateOf(pixelCount(world));
    forEachPixel(world, [&](std::size_t i, double x, double z) {
        const TectonicPlates::Point q = plates.warp(x, z);
        plateOf[i] = static_cast<std::uint8_t>(plates.rawPlateAt(q.x, q.z));
    });
    drawBoundaries(plates, plateOf, world.size, image);
    return image;
}

bool exportVoronoiPngs(const Heightfield& field, const HeightmapRegion& region, int worldSize,
                       const std::filesystem::path& directory) {
    const TectonicPlates* plates = field.plates();
    if (plates == nullptr || worldSize < 2) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    const HeightGrid grid = sampleHeightGrid(field, region);
    const bool map = writePng(directory / "voronoi_plates.png", renderPlatesMap(*plates, region));
    const bool gray = writePng(directory / "voronoi_heightmap.png", renderHeightmapGray(grid));
    const bool preview = writePng(directory / "voronoi_preview.png", renderHeightmapPreview(grid));
    const bool world = writePng(directory / "voronoi_world.png", renderWorldMap(field, worldSize));
    return map && gray && preview && world;
}

} // namespace terrain
