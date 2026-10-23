// Galleries, underground rivers and lava in the cave images (Phase 8C-2): the map of worlds whose
// cave regions all have the profile of the Rivers, Volcanic or Tunnels type (the galleries
// against an independent reading of the columns, the colors of the rivers and of the lava, the
// stats), the sections through a river and through a lava lake (against the densities of a
// sampler of their own), the images without cave regions (unchanged since before the network was
// drawn), and the meeting of two cave region types that holds a preferred one. Small images: the
// build is unoptimized.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_region.hpp"
#include "terrain/io/cave_regions_png.hpp"
#include "terrain/io/caves_png.hpp"
#include "terrain/io/png.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_galleries_test_support.hpp"
#include "caves_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace {

using terrain::CaveColumn;
using terrain::CaveDistrictType;
using terrain::CaveRegion;
using terrain::CaveSlice;
using terrain::HeightmapRegion;
using terrain::Image;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr int kSize = 64;
constexpr double kPixel = 20.0;
constexpr std::size_t kSamples = static_cast<std::size_t>(kSize) * static_cast<std::size_t>(kSize);

using Color = std::array<std::uint8_t, 3>;
using Rgb = const std::uint8_t (&)[3];

Color pixelAt(const Image& image, std::size_t sample) {
    return {image.pixels[sample * 3], image.pixels[sample * 3 + 1], image.pixels[sample * 3 + 2]};
}

Color colorOf(Rgb rgb) {
    return {rgb[0], rgb[1], rgb[2]};
}

bool is(const Color& color, Rgb rgb) {
    return color == colorOf(rgb);
}

std::size_t countPixels(const Image& image, Rgb rgb) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < image.pixels.size() / 3; ++i) {
        count += is(pixelAt(image, i), rgb) ? 1 : 0;
    }
    return count;
}

// A map of 64 x 64 samples at 20 m/px around the origin (land, shore and sea, entrances), of the
// caves of a world whose cave regions all have the profile of one type.
struct Site {
    std::unique_ptr<CaveRegion> region;
    Image preview;
    terrain::CavePreviewStats stats;
};

Site makeSite(const terrain::CaveParams& caves) {
    terrain::CaveRegionParams params = test_relief::onLegacyRelief(terrain::CaveRegionParams{});
    params.caves = caves;
    Site site;
    site.region = std::make_unique<CaveRegion>(kSeed, HeightmapRegion{0.0, 0.0, kPixel, kSize},
                                               params);
    site.preview = terrain::renderCavePreview(*site.region, &site.stats);
    return site;
}

const Site& site(CaveDistrictType type) {
    static const Site rivers = makeSite(galleries_test::uniformParams(CaveDistrictType::Rivers));
    static const Site volcanic =
        makeSite(galleries_test::uniformParams(CaveDistrictType::Volcanic));
    static const Site tunnels = makeSite(galleries_test::uniformParams(CaveDistrictType::Tunnels));
    return type == CaveDistrictType::Rivers ? rivers
                                            : (type == CaveDistrictType::Volcanic ? volcanic
                                                                                  : tunnels);
}

// The column of a sample of the map, as the map reads it: with its aquifer (water, lava) or not.
CaveColumn columnOf(const CaveRegion& region, terrain::AquiferCache& cache, std::size_t sample,
                    bool aquifer = false) {
    const terrain::ErodedRegion& eroded = region.eroded();
    const double x = eroded.sampleX(static_cast<int>(sample) % kSize);
    const double z = eroded.sampleZ(static_cast<int>(sample) / kSize);
    return region.field().column(x, z, region.groundAt(x, z), region.ground(), {}, &cache,
                                 aquifer);
}

// Depth of the river of a column over its floor, meters (0 without river).
double riverDepthOf(const terrain::CaveField& field, const CaveColumn& column) {
    if (column.riverLevel <= terrain::kDryLevel) {
        return 0.0;
    }
    return static_cast<double>(column.riverLevel - column.galleryFloor +
                               field.params().rivers.minDepth);
}

// The samples crossed by a gallery, and those whose gallery may hold a river, read from the
// columns: the axis within max(half width, 0.75 pixel), caves allowed at the middle of the gallery,
// and a river deeper than its minimum depth plus 0.1 m.
struct Network {
    std::vector<bool> gallery = std::vector<bool>(kSamples, false);
    std::vector<bool> river = std::vector<bool>(kSamples, false);
    std::size_t galleries = 0;
    std::size_t rivers = 0;
};

Network networkOf(const CaveRegion& region) {
    const terrain::CaveField& field = region.field();
    const terrain::GalleryParams& galleries = field.params().galleries;
    terrain::AquiferCache cache(field, region.ground());
    Network network;
    for (std::size_t i = 0; i < kSamples; ++i) {
        const CaveColumn column = columnOf(region, cache, i);
        const double presence =
            static_cast<double>(column.profile[terrain::CaveProfile::kGalleries]);
        const double reach = std::max(static_cast<double>(galleries.halfWidth) * presence,
                                      0.75 * kPixel);
        const double middle = static_cast<double>(column.galleryFloor) +
                              static_cast<double>(galleries.halfHeight) * presence;
        if (presence <= 0.0 || static_cast<double>(column.galleryDistance) > reach ||
            field.limit(column, middle) >= 0.0f) {
            continue;
        }
        network.gallery[i] = true;
        ++network.galleries;
        if (riverDepthOf(field, column) >
            static_cast<double>(field.params().rivers.minDepth) + 0.1) {
            network.river[i] = true;
            ++network.rivers;
        }
    }
    return network;
}

double fractionOf(std::size_t count) {
    return static_cast<double>(count) / static_cast<double>(kSamples);
}

// The colors of the galleries, the rivers and the lava stand apart from each other and from the
// other colors of the map (Euclidean distance in RGB).
void testColorsDistinct() {
    const std::array<Color, 3> fresh = {colorOf(terrain::kCaveGalleryRgb),
                                        colorOf(terrain::kCaveRiverWaterRgb),
                                        colorOf(terrain::kCaveLavaRgb)};
    const std::array<Color, 7> others = {
        colorOf(terrain::kCaveWaterRgb),       colorOf(terrain::kCaveRiverRgb),
        colorOf(terrain::kCaveIceRgb),         colorOf(terrain::kCaveEntranceMarkRgb),
        colorOf(terrain::kCaveSliceLineRgb),   colorOf(terrain::kCaveShallowRgb),
        colorOf(terrain::kCaveDeepRgb)};
    double nearest = std::numeric_limits<double>::max();
    for (std::size_t a = 0; a < fresh.size(); ++a) {
        std::vector<Color> rest(others.begin(), others.end());
        rest.insert(rest.end(), fresh.begin() + static_cast<std::ptrdiff_t>(a) + 1, fresh.end());
        for (const Color& other : rest) {
            double sum = 0.0;
            for (std::size_t c = 0; c < 3; ++c) {
                const double d = static_cast<double>(fresh[a][c]) - static_cast<double>(other[c]);
                sum += d * d;
            }
            nearest = std::min(nearest, std::sqrt(sum));
        }
    }
    TEST_CHECK(nearest >= 60.0);
}

// The galleries of the map are those of the columns, mixed with their color (any other mark drawn
// over them aside); a gallery holds no river where its type has none.
void testGalleriesOfTheMap() {
    for (const CaveDistrictType type : {CaveDistrictType::Rivers, CaveDistrictType::Volcanic,
                                        CaveDistrictType::Tunnels}) {
        const Site& s = site(type);
        const Network network = networkOf(*s.region);
        TEST_CHECK(network.galleries > 100);
        TEST_CHECK(s.stats.galleryFraction == fractionOf(network.galleries));
        TEST_CHECK(s.preview.width == kSize && s.preview.height == kSize);
        // Every gallery sample not overwritten by a mark is within the unmixed share of its color.
        const double slack = (1.0 - static_cast<double>(terrain::kCaveGalleryShare)) * 255.0 + 1.0;
        std::size_t checked = 0;
        for (std::size_t i = 0; i < kSamples; ++i) {
            const Color color = pixelAt(s.preview, i);
            if (!network.gallery[i] || is(color, terrain::kCaveRiverWaterRgb) ||
                is(color, terrain::kCaveLavaRgb) || is(color, terrain::kCaveEntranceMarkRgb) ||
                is(color, terrain::kCaveSliceLineRgb)) {
                continue;
            }
            ++checked;
            for (std::size_t c = 0; c < 3; ++c) {
                TEST_CHECK(std::abs(static_cast<double>(color[c]) -
                                    static_cast<double>(terrain::kCaveGalleryRgb[c])) <= slack);
            }
        }
        // (The rivers cover most of the galleries of the Rivers type.)
        TEST_CHECK(type == CaveDistrictType::Rivers || checked * 2 > network.galleries);
    }
}

void testRiversOfTheMap() {
    const Site& s = site(CaveDistrictType::Rivers);
    const Network network = networkOf(*s.region);
    // Rivers lie in the galleries that may hold one, and most of them do.
    TEST_CHECK(network.rivers > 50);
    TEST_CHECK(s.stats.riverFraction > 0.0 && s.stats.riverFraction <= fractionOf(network.rivers));
    TEST_CHECK(s.stats.riverFraction * 2.0 >= fractionOf(network.rivers));
    // They are drawn as they are (the entrances and the line of the section may cover a few), and
    // only there.
    const std::size_t drawn = countPixels(s.preview, terrain::kCaveRiverWaterRgb);
    const double expected = s.stats.riverFraction * static_cast<double>(kSamples);
    TEST_CHECK(static_cast<double>(drawn) <= expected &&
               static_cast<double>(drawn) >= 0.8 * expected);
    for (std::size_t i = 0; i < kSamples; ++i) {
        if (is(pixelAt(s.preview, i), terrain::kCaveRiverWaterRgb)) {
            TEST_CHECK(network.river[i]);
        }
    }
    // The Rivers type has no lava, and the other types no river.
    TEST_CHECK(s.stats.lavaFraction == 0.0 && countPixels(s.preview, terrain::kCaveLavaRgb) == 0);
    for (const CaveDistrictType type : {CaveDistrictType::Volcanic, CaveDistrictType::Tunnels}) {
        TEST_CHECK(site(type).stats.riverFraction == 0.0);
        TEST_CHECK(countPixels(site(type).preview, terrain::kCaveRiverWaterRgb) == 0);
    }
}

void testLavaOfTheMap() {
    const Site& s = site(CaveDistrictType::Volcanic);
    TEST_CHECK(s.stats.lavaFraction > 0.01);
    const std::size_t drawn = countPixels(s.preview, terrain::kCaveLavaRgb);
    const double expected = s.stats.lavaFraction * static_cast<double>(kSamples);
    TEST_CHECK(static_cast<double>(drawn) <= expected &&
               static_cast<double>(drawn) >= 0.8 * expected);
    // Only the cave regions with lava have some.
    TEST_CHECK(site(CaveDistrictType::Tunnels).stats.lavaFraction == 0.0);
    TEST_CHECK(countPixels(site(CaveDistrictType::Tunnels).preview, terrain::kCaveLavaRgb) == 0);
    // Lava lies in the lava aquifers: the column of each lava sample holds some.
    terrain::AquiferCache cache(s.region->field(), s.region->ground());
    for (std::size_t i = 0; i < kSamples; ++i) {
        if (is(pixelAt(s.preview, i), terrain::kCaveLavaRgb)) {
            TEST_CHECK(columnOf(*s.region, cache, i, true).lavaLevel > terrain::kDryLevel);
        }
    }
}

void testDeterministic() {
    const Site& s = site(CaveDistrictType::Rivers);
    terrain::CavePreviewStats again;
    const Image image = terrain::renderCavePreview(*s.region, &again);
    TEST_CHECK(image.pixels == s.preview.pixels);
    TEST_CHECK(again.galleryFraction == s.stats.galleryFraction &&
               again.riverFraction == s.stats.riverFraction &&
               again.lavaFraction == s.stats.lavaFraction &&
               again.cavedFraction == s.stats.cavedFraction);
}


// The first sample of a map of color rgb, or none.
std::optional<std::size_t> firstSample(const Image& image, Rgb rgb) {
    for (std::size_t i = 0; i < image.pixels.size() / 3; ++i) {
        if (is(pixelAt(image, i), rgb)) {
            return i;
        }
    }
    return std::nullopt;
}

// Section of 192 columns at 0.5 m/px along x through the middle of the sample: its column
// width / 2 is at the point of the sample, tall enough for the ground along it and the deepest
// caves under it.
CaveSlice sliceThrough(const CaveRegion& region, std::size_t sample) {
    const terrain::ErodedRegion& eroded = region.eroded();
    CaveSlice slice;
    slice.width = 192;
    slice.metersPerPixel = 0.5;
    slice.centerX = eroded.sampleX(static_cast<int>(sample) % kSize) - slice.metersPerPixel / 2.0;
    slice.centerZ = eroded.sampleZ(static_cast<int>(sample) / kSize);
    double lowest = std::numeric_limits<double>::max();
    double highest = std::numeric_limits<double>::lowest();
    for (int column = 0; column < slice.width; ++column) {
        const double u = slice.centerX + (column + 0.5 - slice.width / 2.0) * slice.metersPerPixel;
        const double ground = static_cast<double>(region.groundAt(u, slice.centerZ));
        lowest = std::min(lowest, ground);
        highest = std::max(highest, ground);
    }
    slice.top = highest + 16.0;
    const double bottom = lowest - static_cast<double>(region.field().params().maxDepth) - 16.0;
    slice.height = static_cast<int>(std::ceil((slice.top - bottom) / slice.metersPerPixel));
    return slice;
}

enum class Fluid { Lava, Water };

// The rows of the middle column of a section that show lava (or water or its ice) against the
// fluid at the same heights from a sampler of its own: returns how many rows show it, and counts
// a failure for every row where the two disagree.
std::size_t checkFluidColumn(const CaveRegion& region, const CaveSlice& slice, const Image& image,
                             Fluid fluid) {
    const int pixelColumn = slice.width / 2;
    const double x = slice.centerX + slice.metersPerPixel / 2.0;
    const double z = slice.centerZ;
    const double bottom = slice.top - slice.height * slice.metersPerPixel;
    terrain::CaveSampler sampler =
        region.sampler({x - 1.0, bottom, z - 1.0, x + 1.0, slice.top, z + 1.0});
    const float ground = region.groundAt(x, z);
    const CaveColumn column = sampler.column(x, z, ground);
    std::size_t shown = 0;
    for (int row = 0; row < slice.height; ++row) {
        const double y = slice.top - (row + 0.5) * slice.metersPerPixel;
        bool expected = false;
        if (y < static_cast<double>(ground)) {
            const auto terrainDensity = static_cast<float>(y - static_cast<double>(ground));
            const float carved = sampler.density(column, y, terrainDensity);
            const float density = fluid == Fluid::Lava
                                      ? sampler.lavaDensity(column, y, terrainDensity, carved)
                                      : sampler.waterDensity(column, y, terrainDensity, carved);
            expected = carved >= 0.0f && density < 0.0f;
        }
        const Color color =
            pixelAt(image, static_cast<std::size_t>(row * slice.width + pixelColumn));
        const bool seen = fluid == Fluid::Lava
                              ? is(color, terrain::kCaveLavaRgb)
                              : (is(color, terrain::kCaveWaterRgb) ||
                                 is(color, terrain::kCaveIceRgb));
        TEST_CHECK(seen == expected);
        shown += seen ? 1 : 0;
    }
    return shown;
}

// A section through a lava sample of the map shows its lava, where the density says so, with
// magma walls around it; the share of lava in its stats is that of its pixels.
void testLavaSection() {
    const Site& s = site(CaveDistrictType::Volcanic);
    const std::optional<std::size_t> spot = firstSample(s.preview, terrain::kCaveLavaRgb);
    TEST_CHECK(spot.has_value());
    if (!spot) {
        return;
    }
    const CaveSlice slice = sliceThrough(*s.region, *spot);
    terrain::CaveSliceStats stats;
    const Image section = terrain::renderCaveSlice(*s.region, slice, &stats);
    const auto pixels = static_cast<double>(section.pixels.size() / 3);
    const std::size_t lava = countPixels(section, terrain::kCaveLavaRgb);
    TEST_CHECK(lava > 0 && stats.lavaFraction == static_cast<double>(lava) / pixels);
    TEST_CHECK(checkFluidColumn(*s.region, slice, section, Fluid::Lava) >= 1);
    const Color magma = terrain::caveMaterialRgb(terrain::CaveMaterial::Magma);
    std::size_t walls = 0;
    for (std::size_t i = 0; i < section.pixels.size() / 3; ++i) {
        walls += pixelAt(section, i) == magma ? 1 : 0;
    }
    TEST_CHECK(walls > 0);
    TEST_CHECK(terrain::renderCaveSlice(*s.region, slice).pixels == section.pixels);
}

// The river sample of the map whose gallery axis is the nearest to its center.
std::optional<std::size_t> axisRiverSample(const Site& s) {
    terrain::AquiferCache cache(s.region->field(), s.region->ground());
    std::optional<std::size_t> best;
    float nearest = 3.0f;
    for (std::size_t i = 0; i < kSamples; ++i) {
        if (!is(pixelAt(s.preview, i), terrain::kCaveRiverWaterRgb)) {
            continue;
        }
        const float distance = columnOf(*s.region, cache, i).galleryDistance;
        if (distance < nearest) {
            nearest = distance;
            best = i;
        }
    }
    return best;
}

// A section through the axis of a river shows its water in the gallery (and the lakes of its
// aquifers where they lie), where the density says so.
void testRiverSection() {
    const Site& s = site(CaveDistrictType::Rivers);
    const std::optional<std::size_t> spot = axisRiverSample(s);
    TEST_CHECK(spot.has_value());
    if (!spot) {
        return;
    }
    const CaveSlice slice = sliceThrough(*s.region, *spot);
    terrain::CaveSliceStats stats;
    const Image section = terrain::renderCaveSlice(*s.region, slice, &stats);
    TEST_CHECK(stats.waterFraction > 0.0 && stats.lavaFraction == 0.0);
    TEST_CHECK(checkFluidColumn(*s.region, slice, section, Fluid::Water) >= 1);
    TEST_CHECK(countPixels(section, terrain::kCaveLavaRgb) == 0);
}

// FNV-1a over the size and the bytes of an image.
std::uint64_t fingerprint(const Image& image) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto add = [&hash](std::uint64_t value) { hash = (hash ^ value) * 1099511628211ULL; };
    add(static_cast<std::uint64_t>(image.width));
    add(static_cast<std::uint64_t>(image.height));
    for (const std::uint8_t byte : image.pixels) {
        add(byte);
    }
    return hash;
}

// Fingerprints of the images of a region without cave regions, which the galleries, rivers and
// lava of the regions leave as they were (regenerated with the wrapped world of Phase 9, the same
// on every platform): land with a river and entrances, 64 px at 32 m/px, and its default section
// cut to 256 columns.
constexpr std::uint64_t kPreviewWithoutRegions = 0xaeae52098453b10dULL;
constexpr std::uint64_t kSliceWithoutRegions = 0xa842ef0ccde95339ULL;

void testWithoutRegionsNothingChanges() {
    const HeightmapRegion area{4000.0, -4000.0, 32.0, 64};
    const CaveRegion region(kSeed, area, test_relief::onLegacyRelief(terrain::CaveRegionParams{}));
    terrain::CavePreviewStats stats;
    const Image preview = terrain::renderCavePreview(region, &stats);
    std::printf("fingerprints without regions: preview 0x%016llx\n",
                static_cast<unsigned long long>(fingerprint(preview)));
    TEST_CHECK(fingerprint(preview) == kPreviewWithoutRegions);
    TEST_CHECK(stats.galleryFraction == 0.0 && stats.riverFraction == 0.0 &&
               stats.lavaFraction == 0.0);
    TEST_CHECK(countPixels(preview, terrain::kCaveRiverWaterRgb) == 0 &&
               countPixels(preview, terrain::kCaveLavaRgb) == 0);
    CaveSlice slice = terrain::defaultCaveSlice(region);
    slice.width = 256;
    terrain::CaveSliceStats sliceStats;
    const Image section = terrain::renderCaveSlice(region, slice, &sliceStats);
    std::printf("fingerprints without regions: section 0x%016llx\n",
                static_cast<unsigned long long>(fingerprint(section)));
    TEST_CHECK(fingerprint(section) == kSliceWithoutRegions);
    TEST_CHECK(sliceStats.lavaFraction == 0.0);

    // Cave regions of the neutral profile only add nothing either: the same images.
    terrain::CaveRegionParams params;
    params.caves.districts = terrain::CaveDistrictParams{};
    params.caves.districts->profiles.fill(terrain::CaveProfile{});
    const CaveRegion neutral(kSeed, area, test_relief::onLegacyRelief(params));
    TEST_CHECK(terrain::renderCavePreview(neutral).pixels == preview.pixels);
}

} // namespace

int main() {
    testColorsDistinct();
    testGalleriesOfTheMap();
    testRiversOfTheMap();
    testLavaOfTheMap();
    testDeterministic();
    testLavaSection();
    testRiverSection();
    testWithoutRegionsNothingChanges();
    return test::result();
}
