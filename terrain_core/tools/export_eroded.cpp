// Writes heightmap_eroded.png, heightmap_eroded_preview.png and rivers.png for visual checks
// of the erosion and river pipeline (square region centered on the origin).
// Usage: terrain_export_eroded <output-dir> [seed] [meters-per-pixel] [size]

#include "terrain/generation/eroded_region.hpp"
#include "terrain/io/eroded_png.hpp"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

template <class T>
bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::HeightmapRegion region{.metersPerPixel = 100.0, .size = 512};
    const bool valid =
        (argc >= 2 && argc <= 5) && (argc < 3 || parse(argv[2], seed)) &&
        (argc < 4 || (parse(argv[3], region.metersPerPixel) && region.metersPerPixel > 0.0)) &&
        (argc < 5 || (parse(argv[4], region.size) && region.size >= 2));
    if (!valid) {
        std::fprintf(stderr, "Usage: %s <output-dir> [seed] [meters-per-pixel] [size]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }

    const terrain::ErodedRegion eroded(seed, region);
    if (!terrain::exportErodedPngs(eroded, argv[1])) {
        std::fprintf(stderr, "Failed to write the eroded heightmaps into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    int riverCells = 0;
    for (const std::uint8_t river : eroded.rivers().river) {
        riverCells += river;
    }
    const double extentKm = region.size * region.metersPerPixel / 1000.0;
    std::printf("seed %llu, %d x %d px, %.1f km wide, %d river cells, %zu cascades: "
                "%s/heightmap_eroded.png, %s/heightmap_eroded_preview.png, %s/rivers.png\n",
                static_cast<unsigned long long>(seed), region.size, region.size, extentKm,
                riverCells, eroded.rivers().cascades.size(), argv[1], argv[1], argv[1]);
    return EXIT_SUCCESS;
}
