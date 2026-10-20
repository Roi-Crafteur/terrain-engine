// Writes cave_regions.png and cave_regions_preview.png for visual checks of the cave districts
// (Phase 8C-1): the map of the blended district types of the streamed world, square and
// centered on (cx, cz), and the map of the caves of a region with districts around the nearest
// point to that center where two types meet, one of them if possible the Rivers or the Volcanic
// type, which hold the underground rivers and the lava (224 px at 20 m/px, about fifteen seconds
// in an unoptimized build: its size is fixed).
// Usage: terrain_export_cave_regions <output-dir> [seed] [meters-per-pixel] [size] [cx] [cz]
// The map is 512 px at 100 m/px (51.2 km) around the origin by default; a district is 2 to 4 km
// across, so keep meters-per-pixel below 400 to see the borders.

#include "terrain/io/cave_regions_png.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

// Largest map: 4096 x 4096 pixels hold a few hundred megabytes of grids and images.
constexpr int kMaxSize = 4096;

template <class T>
bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

// Fills the request from the arguments; returns the problem with them, or nullptr if valid.
const char* parseArguments(int argc, char** argv, terrain::CaveRegionsExport& request) {
    terrain::HeightmapRegion& map = request.map;
    if (argc < 2 || argc > 7) {
        return "expected 1 to 6 arguments";
    }
    if (argc >= 3 && !parse(argv[2], request.seed)) {
        return "seed must be a non-negative integer";
    }
    if (argc >= 4 && !(parse(argv[3], map.metersPerPixel) && map.metersPerPixel > 0.0 &&
                       std::isfinite(map.metersPerPixel))) {
        return "meters-per-pixel must be a positive number";
    }
    if (argc >= 5 && !(parse(argv[4], map.size) && map.size >= 2 && map.size <= kMaxSize)) {
        return "size must be an integer from 2 to 4096";
    }
    if (argc >= 6 && !(parse(argv[5], map.centerX) && std::isfinite(map.centerX))) {
        return "cx must be a finite number";
    }
    if (argc >= 7 && !(parse(argv[6], map.centerZ) && std::isfinite(map.centerZ))) {
        return "cz must be a finite number";
    }
    return nullptr;
}

// Types of a non-zero share: "tunnels 18.2%, halls 4.0%, ..." in the order of the enumeration,
// or, if sorted, "dripstone 61% / lakes 39%" by decreasing share, those of at least 0.5% only.
void printShares(const std::array<double, terrain::kCaveDistrictTypeCount>& shares,
                 bool sorted) {
    std::array<std::size_t, terrain::kCaveDistrictTypeCount> order{};
    for (std::size_t t = 0; t < order.size(); ++t) {
        order[t] = t;
    }
    if (sorted) {
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return shares[a] > shares[b]; });
    }
    const double minShare = sorted ? 0.005 : 0.0;
    const char* separator = "";
    for (const std::size_t t : order) {
        if (shares[t] > minShare) {
            std::printf("%s%s %.*f%%", separator,
                        terrain::caveDistrictName(static_cast<terrain::CaveDistrictType>(t)),
                        sorted ? 0 : 1, 100.0 * shares[t]);
            separator = sorted ? " / " : ", ";
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    terrain::CaveRegionsExport request;
    if (const char* problem = parseArguments(argc, argv, request); problem != nullptr) {
        std::fprintf(stderr, "Error: %s\nUsage: %s <output-dir> [seed] [meters-per-pixel] [size] "
                             "[cx] [cz]\n",
                     problem, argv[0]);
        return EXIT_FAILURE;
    }

    request.preferredTypes = {terrain::CaveDistrictType::Rivers,
                              terrain::CaveDistrictType::Volcanic};
    terrain::CaveRegionsStats stats;
    if (!terrain::exportCaveRegionsPngs(request, argv[1], &stats)) {
        std::fprintf(stderr, "Failed to write the cave region images into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const terrain::HeightmapRegion& map = request.map;
    std::printf("cave regions %d x %d px, %.1f km wide, %d types over the ground where caves can "
                "exist: ",
                map.size, map.size, map.size * map.metersPerPixel / 1000.0,
                stats.map.typesPresent);
    printShares(stats.map.typeShares, false);
    const terrain::HeightmapRegion& area = stats.previewArea;
    std::printf("; preview %d x %d px, %.1f km wide at (%.0f, %.0f), types ", area.size,
                area.size, area.size * area.metersPerPixel / 1000.0, area.centerX, area.centerZ);
    printShares(stats.preview.typeShares, true);
    const terrain::CavePreviewStats& caves = stats.preview.caves;
    std::printf(", caves under %.1f%% of the map, lakes under %.1f%%, galleries over %.1f%%, "
                "rivers over %.1f%%, lava under %.1f%%, %zu entrances; seed %llu: "
                "%s/cave_regions.png, %s/cave_regions_preview.png\n",
                100.0 * caves.cavedFraction, 100.0 * caves.lakeFraction,
                100.0 * caves.galleryFraction, 100.0 * caves.riverFraction,
                100.0 * caves.lavaFraction, caves.entrances,
                static_cast<unsigned long long>(request.seed), argv[1], argv[1]);
    return EXIT_SUCCESS;
}
