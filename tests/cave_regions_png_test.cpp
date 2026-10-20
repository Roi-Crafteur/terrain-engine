// Cave regions images: the map of the district types of the streamed world (colors, blend across
// the borders, shading, sea, marks, stats), the meeting of two types, the preview of a region
// with districts, and the files of the export. Small images: the world is the default one.
#include "terrain/generation/cave_region.hpp"
#include "terrain/io/cave_regions_png.hpp"
#include "terrain/io/caves_png.hpp"
#include "terrain/io/heightmap_png.hpp"
#include "terrain/io/png.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using terrain::AquiferCache;
using terrain::CaveDistrict;
using terrain::CaveDistrictType;
using terrain::CaveRegion;
using terrain::DistrictWeights;
using terrain::HeightmapRegion;
using terrain::Image;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr std::size_t kTypes = terrain::kCaveDistrictTypeCount;

using Color = std::array<double, 3>;
using Shares = std::array<double, kTypes>;

terrain::CaveParams districtParams() {
    terrain::CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    return params;
}

const terrain::TerrainTiles& world() {
    static const terrain::TerrainTiles tiles(kSeed, {}, {}, districtParams());
    return tiles;
}

const terrain::CaveField& field() {
    return *world().caves();
}

Color pixel(const Image& image, int column, int row) {
    const auto offset = static_cast<std::size_t>((row * image.width + column) * 3);
    return {static_cast<double>(image.pixels[offset]),
            static_cast<double>(image.pixels[offset + 1]),
            static_cast<double>(image.pixels[offset + 2])};
}

Color colorOf(CaveDistrictType type) {
    const std::array<std::uint8_t, 3> rgb = terrain::caveDistrictRgb(type);
    return {static_cast<double>(rgb[0]), static_cast<double>(rgb[1]), static_cast<double>(rgb[2])};
}

// Share of each channel in the sum of the three: what the shading, which scales all of them
// alike, leaves of a color.
Color chroma(const Color& color) {
    const double sum = color[0] + color[1] + color[2];
    return {color[0] / sum, color[1] / sum, color[2] / sum};
}

double distance(const Color& a, const Color& b) {
    return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
}

// The weight of each district at a point, by index of its type.
std::vector<std::pair<std::size_t, double>> typeWeights(const DistrictWeights& weights) {
    std::vector<std::pair<std::size_t, double>> result;
    for (std::size_t i = 0; i < static_cast<std::size_t>(weights.count); ++i) {
        result.emplace_back(static_cast<std::size_t>(weights.districts[i].type),
                            static_cast<double>(weights.weights[i]));
    }
    return result;
}

// Sum of the weights times the colors of the districts.
Color mixOf(const DistrictWeights& weights) {
    Color mix{};
    for (const auto& [type, weight] : typeWeights(weights)) {
        const Color color = colorOf(static_cast<CaveDistrictType>(type));
        for (std::size_t c = 0; c < 3; ++c) {
            mix[c] += weight * color[c];
        }
    }
    return mix;
}

// Weight of the second heaviest type: the types of the districts added up.
double secondWeight(const DistrictWeights& weights) {
    Shares byType{};
    for (const auto& [type, weight] : typeWeights(weights)) {
        byType[type] += weight;
    }
    std::sort(byType.begin(), byType.end(), std::greater<>());
    return byType[1];
}

// Ground at least 20 m over the sea at (x, z) and 700 m away in the four directions.
bool landAround(const terrain::CaveGround& ground, double x, double z) {
    const auto land = [&](double dx, double dz) { return ground.height(x + dx, z + dz) >= 20.0f; };
    return land(0.0, 0.0) && land(700.0, 0.0) && land(-700.0, 0.0) && land(0.0, 700.0) &&
           land(0.0, -700.0);
}

// The first district of the cells [-6, 6]^2, in cell order, on land all around.
std::optional<CaveDistrict> landSite() {
    const terrain::CaveGround& ground = world().caveGround();
    for (int cellZ = -6; cellZ <= 6; ++cellZ) {
        for (int cellX = -6; cellX <= 6; ++cellX) {
            const CaveDistrict district = field().district(cellX, cellZ, ground);
            if (landAround(ground, district.siteX, district.siteZ)) {
                return district;
            }
        }
    }
    return std::nullopt;
}

struct SitePair {
    CaveDistrict a;
    CaveDistrict b;
    double x = 0.0; // of the midpoint
    double z = 0.0;
};

// The first pair of east neighbors of different hues whose midpoint is on land all around, where
// the two types weigh at least 0.3 each.
std::optional<SitePair> landPair(const terrain::CaveField& districts,
                                 const terrain::CaveGround& ground) {
    AquiferCache cache(districts, ground);
    for (int cellZ = -6; cellZ <= 6; ++cellZ) {
        for (int cellX = -6; cellX <= 6; ++cellX) {
            const CaveDistrict& a = cache.district(cellX, cellZ);
            const CaveDistrict& b = cache.district(cellX + 1, cellZ);
            const SitePair pair{a, b, (a.siteX + b.siteX) / 2.0, (a.siteZ + b.siteZ) / 2.0};
            if (a.type != b.type && landAround(ground, pair.x, pair.z) &&
                distance(chroma(colorOf(a.type)), chroma(colorOf(b.type))) >= 0.15 &&
                secondWeight(cache.districtWeights(pair.x, pair.z)) >= 0.3) {
                return pair;
            }
        }
    }
    return std::nullopt;
}

template <class Call>
bool throwsInvalidArgument(Call call) {
    try {
        call();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void testColorsDistinct() {
    double nearest = std::numeric_limits<double>::max();
    for (std::size_t a = 0; a < kTypes; ++a) {
        const auto typeA = static_cast<CaveDistrictType>(a);
        TEST_CHECK(terrain::caveDistrictRgb(typeA) == terrain::kCaveDistrictRgb[a]);
        for (std::size_t b = a + 1; b < kTypes; ++b) {
            const Color ca = colorOf(typeA);
            const Color cb = colorOf(static_cast<CaveDistrictType>(b));
            nearest = std::min(nearest, std::hypot(ca[0] - cb[0], ca[1] - cb[1], ca[2] - cb[2]));
        }
    }
    TEST_CHECK(nearest >= 60.0); // Euclidean distance in RGB
}

void testSiteHasItsTypeColor() {
    const std::optional<CaveDistrict> site = landSite();
    TEST_CHECK(site.has_value());
    if (!site) {
        return;
    }
    // 33 px at 100 m/px centered on the site: pixel (16, 16) is its own.
    const HeightmapRegion area{site->siteX, site->siteZ, 100.0, 33};
    const Image map = terrain::renderCaveRegionsMap(field(), world().caveGround(), area);
    TEST_CHECK(map.width == 33 && map.height == 33 && map.channels == 3);
    // 400 m from the site, 1100 m or more from any other: its own color, shaded.
    const Color shaded = chroma(pixel(map, 20, 16));
    TEST_CHECK(distance(shaded, chroma(colorOf(site->type))) < 0.02);
    TEST_CHECK(distance(shaded, chroma(pixel(map, 16, 20))) < 0.02);
    // The mark of the site: white, with a black outline one pixel outside it.
    const Color white{255.0, 255.0, 255.0};
    TEST_CHECK(pixel(map, 16, 16) == white && pixel(map, 17, 15) == white);
    TEST_CHECK((pixel(map, 18, 16) == Color{0.0, 0.0, 0.0}));
}

void testColorsBlendAcrossBorder() {
    const std::optional<SitePair> pair = landPair(field(), world().caveGround());
    TEST_CHECK(pair.has_value());
    if (!pair) {
        return;
    }
    AquiferCache cache(field(), world().caveGround());
    const HeightmapRegion area{pair->x, pair->z, 100.0, 33};
    const Image map = terrain::renderCaveRegionsMap(field(), world().caveGround(), area);
    // Pixel (16, 16) is the midpoint of the two sites, on the border between them.
    const Color expected = chroma(mixOf(cache.districtWeights(pair->x, pair->z)));
    const Color halfway = chroma(pixel(map, 16, 16));
    TEST_CHECK(distance(halfway, expected) < 0.03);
    TEST_CHECK(distance(halfway, chroma(colorOf(pair->a.type))) > 0.04);
    TEST_CHECK(distance(halfway, chroma(colorOf(pair->b.type))) > 0.04);
}

// On flat ground, where the relief casts no shade, the colors are exact: the pixels near the
// border between two districts are the mix of their colors, or half of it on the line of the
// border, which is thin.
void testBorderIsAThinLine() {
    const terrain::CaveField districts(kSeed, districtParams());
    const terrain::CaveGround ground = caves_test::flatGround(100.0f);
    const std::optional<SitePair> pair = landPair(districts, ground);
    TEST_CHECK(pair.has_value());
    if (!pair) {
        return;
    }
    AquiferCache cache(districts, ground);
    const HeightmapRegion area{pair->x, pair->z, 100.0, 33};
    const Image map = terrain::renderCaveRegionsMap(districts, ground, area);
    int whole = 0;
    int half = 0;
    for (int row = 13; row <= 19; ++row) {
        for (int column = 13; column <= 19; ++column) {
            const double x = pair->x + (column - 16) * 100.0;
            const double z = pair->z + (row - 16) * 100.0;
            const Color mix = mixOf(cache.districtWeights(x, z));
            const Color seen = pixel(map, column, row);
            const bool isWhole = distance(seen, mix) <= 1.0;
            const bool isHalf = distance(seen, {mix[0] / 2, mix[1] / 2, mix[2] / 2}) <= 1.0;
            TEST_CHECK(isWhole || isHalf);
            whole += isWhole ? 1 : 0;
            half += isHalf ? 1 : 0;
        }
    }
    TEST_CHECK(half >= 5 && half <= 20 && whole >= 25);
}

using Points = std::vector<std::pair<double, double>>;

// The points of the pixels of an area that satisfy keep.
Points pixelPoints(const HeightmapRegion& area, const std::function<bool(double, double)>& keep) {
    Points points;
    for (int row = 0; row < area.size; ++row) {
        for (int column = 0; column < area.size; ++column) {
            const double x = area.centerX + (column + 0.5 - area.size / 2.0) * area.metersPerPixel;
            const double z = area.centerZ + (row + 0.5 - area.size / 2.0) * area.metersPerPixel;
            if (keep(x, z)) {
                points.emplace_back(x, z);
            }
        }
    }
    return points;
}

// The mean weights of the districts at the points, as the stats of an image report them.
Shares meanWeights(const terrain::CaveField& districts, const terrain::CaveGround& ground,
                   const Points& points) {
    AquiferCache cache(districts, ground);
    Shares shares{};
    for (const auto& [x, z] : points) {
        for (const auto& [type, weight] : typeWeights(cache.districtWeights(x, z))) {
            shares[type] += weight;
        }
    }
    for (double& share : shares) {
        share /= static_cast<double>(points.size());
    }
    return shares;
}

// The shares are the expected mean weights, and sum to 1.
void checkShares(const Shares& shares, const Shares& expected) {
    double sum = 0.0;
    for (std::size_t t = 0; t < kTypes; ++t) {
        sum += shares[t];
        TEST_CHECK(std::abs(shares[t] - expected[t]) < 1.0e-9);
    }
    TEST_CHECK(std::abs(sum - 1.0) < 1.0e-9);
}

void testMapStats() {
    const HeightmapRegion area{0.0, 0.0, 100.0, 64};
    terrain::CaveRegionsMapStats stats;
    const Image map = terrain::renderCaveRegionsMap(field(), world().caveGround(), area, &stats);
    const Image again = terrain::renderCaveRegionsMap(field(), world().caveGround(), area);
    TEST_CHECK(map.pixels == again.pixels); // deterministic

    // Mean weights over the pixels where caves can exist (ground above -deepOceanDepth).
    const double limit = -static_cast<double>(field().params().deepOceanDepth);
    const Points points = pixelPoints(area, [&](double x, double z) {
        return world().caveGround().height(x, z) > limit;
    });
    TEST_CHECK(!points.empty());
    checkShares(stats.typeShares, meanWeights(field(), world().caveGround(), points));
    const auto present = std::count_if(stats.typeShares.begin(), stats.typeShares.end(),
                                       [](double share) { return share > 0.0; });
    TEST_CHECK(present == stats.typesPresent && present >= 2);
    TEST_CHECK(std::abs(stats.cavedGroundFraction - static_cast<double>(points.size()) /
                                                        (area.size * area.size)) < 1.0e-12);
    TEST_CHECK(stats.sites >= 1 && stats.sites <= 9); // 6.4 km wide: 2 x 2 to 3 x 3 districts
}

// On flat ground the relief casts no shade: the district colors are exact, under the sea
// dimmed to 0.7 of them and where it is too deep for caves to 0.35.
void testSeaDimsTheMap() {
    const terrain::CaveField districts(kSeed, districtParams());
    const float depth = districts.params().deepOceanDepth;
    const float heights[3] = {100.0f, -50.0f, -depth - 300.0f};
    const float lights[3] = {1.0f, 0.7f, 0.35f};
    for (int h = 0; h < 3; ++h) {
        const terrain::CaveGround ground = caves_test::flatGround(heights[h]);
        const CaveDistrict site = districts.district(0, 0, ground);
        const HeightmapRegion area{site.siteX, site.siteZ, 100.0, 9};
        terrain::CaveRegionsMapStats stats;
        const Image map = terrain::renderCaveRegionsMap(districts, ground, area, &stats);
        const Color color = colorOf(site.type);
        const double light = static_cast<double>(lights[h]);
        // 300 m from the site: its own district alone.
        TEST_CHECK(distance(pixel(map, 7, 4), {color[0] * light, color[1] * light,
                                               color[2] * light}) <= 1.0);
        const bool caves = h < 2;
        TEST_CHECK(stats.cavedGroundFraction == (caves ? 1.0 : 0.0));
        TEST_CHECK(caves ? stats.typeShares[static_cast<std::size_t>(site.type)] > 0.95
                         : stats.typesPresent == 0);
        TEST_CHECK(stats.sites == 1);
    }
}

void testBadArguments() {
    const terrain::CaveField plain(kSeed);
    const terrain::CaveGround ground = caves_test::flatGround(10.0f);
    const HeightmapRegion area{0.0, 0.0, 100.0, 8};
    TEST_CHECK(throwsInvalidArgument(
        [&] { (void)terrain::renderCaveRegionsMap(plain, ground, area); }));
    TEST_CHECK(throwsInvalidArgument(
        [&] { (void)terrain::findCaveRegionsMeeting(plain, ground, 0.0, 0.0); }));
    const HeightmapRegion empty{0.0, 0.0, 100.0, 0};
    TEST_CHECK(throwsInvalidArgument(
        [&] { (void)terrain::renderCaveRegionsMap(field(), ground, empty); }));
    const CaveRegion regionWithoutDistricts(kSeed, HeightmapRegion{0.0, 0.0, 40.0, 16});
    TEST_CHECK(throwsInvalidArgument(
        [&] { (void)terrain::renderCaveRegionsPreview(regionWithoutDistricts); }));
}

terrain::CaveRegionsMeeting findMeeting(const terrain::CaveGround& ground, double x, double z) {
    return terrain::findCaveRegionsMeeting(field(), ground, x, z);
}

void testMeeting() {
    const terrain::CaveGround& ground = world().caveGround();
    const terrain::CaveRegionsMeeting meeting = findMeeting(ground, 0.0, 0.0);
    const terrain::CaveRegionsMeeting again = findMeeting(ground, 0.0, 0.0);
    TEST_CHECK(meeting.found && again.found && again.x == meeting.x && again.z == meeting.z);
    TEST_CHECK(ground.height(meeting.x, meeting.z) >= 10.0f);
    AquiferCache cache(field(), ground);
    TEST_CHECK(secondWeight(cache.districtWeights(meeting.x, meeting.z)) >= 0.3);
    // It is the nearest meeting: asking from it finds it again.
    const terrain::CaveRegionsMeeting from = findMeeting(ground, meeting.x, meeting.z);
    TEST_CHECK(from.found && from.x == meeting.x && from.z == meeting.z);
    // No land, no meeting: the point itself.
    const terrain::CaveGround sea = caves_test::flatGround(-500.0f);
    const terrain::CaveRegionsMeeting none = findMeeting(sea, 123.0, -456.0);
    TEST_CHECK(!none.found && none.x == 123.0 && none.z == -456.0);
}

// The preview region: 48 px at 40 m/px (1.9 km) around the meeting of two types.
const CaveRegion& previewRegion() {
    static const terrain::CaveRegionsMeeting meeting = findMeeting(world().caveGround(), 0.0, 0.0);
    terrain::CaveRegionParams params;
    params.caves = districtParams();
    static const CaveRegion region(kSeed, HeightmapRegion{meeting.x, meeting.z, 40.0, 48}, params);
    return region;
}

void testPreview() {
    const CaveRegion& region = previewRegion();
    terrain::CaveRegionsPreviewStats stats;
    const Image preview = terrain::renderCaveRegionsPreview(region, &stats);
    terrain::CaveRegionsPreviewStats statsAgain;
    const Image again = terrain::renderCaveRegionsPreview(region, &statsAgain);
    TEST_CHECK(preview.width == 48 && preview.height == 48 && preview.channels == 3);
    TEST_CHECK(preview.pixels == again.pixels && stats.typeShares == statsAgain.typeShares);

    // The caves are those of cave_preview, drawn over the districts.
    terrain::CavePreviewStats plainStats;
    const Image plain = terrain::renderCavePreview(region, &plainStats);
    TEST_CHECK(stats.caves.cavedFraction > 0.02);
    TEST_CHECK(stats.caves.cavedFraction == plainStats.cavedFraction &&
               stats.caves.lakeFraction == plainStats.lakeFraction &&
               stats.caves.entrances == plainStats.entrances);

    // The two types of the meeting both weigh on it, and the shares are the mean weights.
    const terrain::ErodedRegion& eroded = region.eroded();
    const Points points = pixelPoints(eroded.region(), [](double, double) { return true; });
    checkShares(stats.typeShares, meanWeights(region.field(), region.ground(), points));
    Shares sorted = stats.typeShares;
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    TEST_CHECK(sorted[1] >= 0.05);

    // Where cave_preview shows the plain relief (no river, cave, entrance or line), the preview
    // is the relief tinted with the district mix of the point.
    const Image relief = terrain::renderHeightmapPreview(eroded.grid());
    AquiferCache cache(region.field(), region.ground());
    int checked = 0;
    for (int row = 0; row < 48; ++row) {
        for (int column = 0; column < 48; ++column) {
            if (pixel(plain, column, row) != pixel(relief, column, row)) {
                continue;
            }
            const Color mix =
                mixOf(cache.districtWeights(eroded.sampleX(column), eroded.sampleZ(row)));
            const Color base = pixel(relief, column, row);
            const Color seen = pixel(preview, column, row);
            const auto share = static_cast<double>(terrain::kCaveRegionsPreviewTintShare);
            const Color tinted{base[0] + (mix[0] - base[0]) * share,
                               base[1] + (mix[1] - base[1]) * share,
                               base[2] + (mix[2] - base[2]) * share};
            TEST_CHECK(distance(seen, tinted) <= 1.0);
            ++checked;
        }
    }
    TEST_CHECK(checked >= 100);
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// The files are the images rendered above: a second, independent render of both.
void testExportWritesTwoImages() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_cave_regions_png_test" / "nested";
    std::filesystem::remove_all(directory.parent_path());
    terrain::CaveRegionsExport request;
    request.map = HeightmapRegion{0.0, 0.0, 100.0, 48};
    request.previewMetersPerPixel = 40.0;
    request.previewSize = 48;
    terrain::CaveRegionsStats stats;
    TEST_CHECK(terrain::exportCaveRegionsPngs(request, directory, &stats));

    const terrain::CaveRegionsMeeting meeting = findMeeting(world().caveGround(), 0.0, 0.0);
    TEST_CHECK(stats.meetingFound && stats.previewArea.centerX == meeting.x &&
               stats.previewArea.centerZ == meeting.z && stats.previewArea.size == 48);
    TEST_CHECK(stats.map.sites >= 1 && stats.map.typesPresent >= 1);

    const Image map = terrain::renderCaveRegionsMap(field(), world().caveGround(), request.map);
    TEST_CHECK(readFile(directory / "cave_regions.png") == terrain::encodePng(map));
    const Image preview = terrain::renderCaveRegionsPreview(previewRegion());
    TEST_CHECK(readFile(directory / "cave_regions_preview.png") == terrain::encodePng(preview));
    std::filesystem::remove_all(directory.parent_path());

    // Bad requests: a preview of 1 px throws, a directory that is a regular file fails.
    request.previewSize = 1;
    TEST_CHECK(throwsInvalidArgument(
        [&] { (void)terrain::exportCaveRegionsPngs(request, directory); }));
    const std::filesystem::path file = directory.parent_path().string() + "_file";
    std::ofstream(file) << "not a directory";
    TEST_CHECK(!terrain::exportCaveRegionsPngs(terrain::CaveRegionsExport{}, file));
    std::filesystem::remove_all(file);
}

} // namespace

int main() {
    testColorsDistinct();
    testSiteHasItsTypeColor();
    testColorsBlendAcrossBorder();
    testBorderIsAThinLine();
    testMapStats();
    testSeaDimsTheMap();
    testBadArguments();
    testMeeting();
    testPreview();
    testExportWritesTwoImages();
    return test::result();
}
