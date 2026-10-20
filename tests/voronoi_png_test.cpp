#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/io/heightmap_png.hpp"
#include "terrain/io/png.hpp"
#include "terrain/io/voronoi_png.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

namespace {

using terrain::BoundaryKind;
using terrain::Heightfield;
using terrain::HeightfieldParams;
using terrain::HeightmapRegion;
using terrain::Image;
using terrain::PlateColor;
using terrain::PlateKind;
using terrain::TectonicPlates;

HeightfieldParams withPlates() {
    HeightfieldParams params;
    params.plates.amplitude = 0.5f;
    params.macroContinent = 0.25f;
    return params;
}

PlateColor pixel(const Image& image, std::size_t i) {
    return {image.pixels[3 * i], image.pixels[3 * i + 1], image.pixels[3 * i + 2]};
}

int countColor(const Image& image, PlateColor color) {
    int count = 0;
    for (std::size_t i = 0; i < image.pixels.size() / 3; ++i) {
        count += pixel(image, i) == color ? 1 : 0;
    }
    return count;
}

void testColors() {
    std::set<PlateColor> kinds;
    for (const PlateKind kind : {PlateKind::Continent, PlateKind::Ocean, PlateKind::InlandSea,
                                 PlateKind::ContinentalShelf}) {
        kinds.insert(terrain::plateKindColor(kind));
    }
    std::set<PlateColor> boundaries;
    for (const BoundaryKind kind :
         {BoundaryKind::Collision, BoundaryKind::Divergence, BoundaryKind::Transform}) {
        boundaries.insert(terrain::boundaryKindColor(kind));
    }
    TEST_CHECK(kinds.size() == 4);
    TEST_CHECK(boundaries.size() == 3);
    TEST_CHECK(terrain::boundaryKindColor(BoundaryKind::Collision) == terrain::kCollisionColor);
    // Land reads warm, water blue.
    const PlateColor land = terrain::plateKindColor(PlateKind::Continent);
    TEST_CHECK(land[0] > land[2]);
    for (const PlateKind water :
         {PlateKind::Ocean, PlateKind::InlandSea, PlateKind::ContinentalShelf}) {
        const PlateColor c = terrain::plateKindColor(water);
        TEST_CHECK(c[2] > c[0]);
    }
}

void testPlanetRegion() {
    const Heightfield field(terrain::kDefaultWorldSeed, withPlates());
    const TectonicPlates& plates = *field.plates();
    const HeightmapRegion region = terrain::planetRegion(plates, 200);
    TEST_CHECK(region.size == 200);
    TEST_CHECK(region.centerX == 0.0 && region.centerZ == 0.0);
    TEST_CHECK(region.metersPerPixel * 200 == plates.world().x);
}

// The whole planet at 192 px: every plate kind of the planet is filled with its color away
// from the boundaries, and the boundaries of the three kinds and the coastline are drawn.
void testPlatesMap() {
    const Heightfield field(terrain::kDefaultWorldSeed, withPlates());
    const TectonicPlates& plates = *field.plates();
    const HeightmapRegion region = terrain::planetRegion(plates, 192);
    const Image image = terrain::renderPlatesMap(plates, region);
    TEST_CHECK(image.width == 192 && image.height == 192 && image.channels == 3);
    TEST_CHECK(image.pixels.size() == 192u * 192u * 3u);
    TEST_CHECK(countColor(image, terrain::kCollisionColor) > 0);
    TEST_CHECK(countColor(image, terrain::kDivergenceColor) > 0);
    TEST_CHECK(countColor(image, terrain::kTransformColor) > 0);
    TEST_CHECK(countColor(image, terrain::kPlateCoastColor) > 0);

    // Pixel centers far inside a plate: the plate color, warm on land, blue on water.
    bool fills = true;
    int interiors = 0;
    const double half = region.size / 2.0;
    for (int row = 0; row < region.size; row += 7) {
        for (int column = 0; column < region.size; column += 7) {
            const double x = region.centerX + (column + 0.5 - half) * region.metersPerPixel;
            const double z = region.centerZ + (row + 0.5 - half) * region.metersPerPixel;
            const terrain::PlateSample s = plates.sampleAt(x, z);
            if (s.coast > 0.0f || s.collision > 0.0f || s.rift != 0.0f ||
                s.boundaryDistance < 1'000'000.0) {
                continue;
            }
            ++interiors;
            const PlateColor c = pixel(image, static_cast<std::size_t>(row * region.size + column));
            const bool land =
                plates.plates()[static_cast<std::size_t>(s.plate)].kind == PlateKind::Continent;
            fills = fills && (land ? c[0] > c[2] : c[2] > c[0]);
        }
    }
    TEST_CHECK(fills);
    TEST_CHECK(interiors > 50);
}

// The world map is the relief preview of the planet with the boundaries over it.
void testWorldMap() {
    const Heightfield field(terrain::kDefaultWorldSeed, withPlates());
    const Image world = terrain::renderWorldMap(field, 128);
    const Image preview = terrain::renderHeightmapPreview(
        terrain::sampleHeightGrid(field, terrain::planetRegion(*field.plates(), 128)));
    TEST_CHECK(world.width == 128 && world.height == 128 && world.channels == 3);
    int changed = 0;
    bool boundaryColors = true;
    for (std::size_t i = 0; i < world.pixels.size() / 3; ++i) {
        if (pixel(world, i) != pixel(preview, i)) {
            ++changed;
            const PlateColor c = pixel(world, i);
            boundaryColors =
                boundaryColors && (c == terrain::kCollisionColor ||
                                   c == terrain::kDivergenceColor || c == terrain::kTransformColor);
        }
    }
    TEST_CHECK(boundaryColors);
    TEST_CHECK(changed > 128 && changed < 128 * 128 / 4);
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void testExport() {
    const Heightfield field(terrain::kDefaultWorldSeed, withPlates());
    const HeightmapRegion region{
        .centerX = 1.0e6, .centerZ = -2.0e6, .metersPerPixel = 400'000.0, .size = 48};
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_voronoi_png_test";
    std::filesystem::remove_all(directory);
    TEST_CHECK(terrain::exportVoronoiPngs(field, region, 40, directory));

    const terrain::HeightGrid grid = terrain::sampleHeightGrid(field, region);
    TEST_CHECK(readFile(directory / "voronoi_plates.png") ==
               terrain::encodePng(terrain::renderPlatesMap(*field.plates(), region)));
    TEST_CHECK(readFile(directory / "voronoi_heightmap.png") ==
               terrain::encodePng(terrain::renderHeightmapGray(grid)));
    TEST_CHECK(readFile(directory / "voronoi_preview.png") ==
               terrain::encodePng(terrain::renderHeightmapPreview(grid)));
    TEST_CHECK(readFile(directory / "voronoi_world.png") ==
               terrain::encodePng(terrain::renderWorldMap(field, 40)));
    std::filesystem::remove_all(directory);

    // Nothing to export without plates, or with a world map of a single pixel; a file in the
    // way of the directory makes the export fail.
    TEST_CHECK(!terrain::exportVoronoiPngs(Heightfield(terrain::kDefaultWorldSeed), region, 40,
                                           directory));
    TEST_CHECK(!std::filesystem::exists(directory));
    TEST_CHECK(!terrain::exportVoronoiPngs(field, region, 1, directory));
    const std::filesystem::path blocker =
        std::filesystem::temp_directory_path() / "terrain_voronoi_png_blocker";
    std::filesystem::remove_all(blocker);
    std::ofstream(blocker) << "not a directory";
    TEST_CHECK(!terrain::exportVoronoiPngs(field, region, 40, blocker / "maps"));
    std::filesystem::remove_all(blocker);
}

} // namespace

int main() {
    testColors();
    testPlanetRegion();
    testPlatesMap();
    testWorldMap();
    testExport();
    return test::result();
}
