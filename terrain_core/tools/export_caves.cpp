// Writes cave_slice.png and cave_preview.png for visual checks of the caves under an eroded
// region with its rivers (square region centered on the cliff site of the world, see
// findCliffRegion: the same region as terrain_export_cliffs with the same arguments).
// Usage: terrain_export_caves <output-dir> [seed] [meters-per-pixel] [size]

#include "terrain/generation/cave_region.hpp"
#include "terrain/generation/cliffs.hpp"
#include "terrain/io/caves_png.hpp"

#include <charconv>
#include <cmath>
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
    // 4.5 km around the cliff site, with a river: the region is eroded and every sample is
    // scanned for caves, which an unoptimized build does in about fifteen seconds.
    double metersPerPixel = 20.0;
    int size = 224;
    const bool valid = (argc >= 2 && argc <= 5) && (argc < 3 || parse(argv[2], seed)) &&
                       (argc < 4 || (parse(argv[3], metersPerPixel) && metersPerPixel > 0.0 &&
                                     std::isfinite(metersPerPixel))) &&
                       (argc < 5 || (parse(argv[4], size) && size >= 2));
    if (!valid) {
        std::fprintf(stderr, "Usage: %s <output-dir> [seed] [meters-per-pixel] [size]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }

    const terrain::HeightmapRegion area = terrain::findCliffRegion(seed, metersPerPixel, size);
    const terrain::CaveRegion region(seed, area);
    terrain::CavePreviewStats stats;
    if (!terrain::exportCavePngs(region, argv[1], &stats)) {
        std::fprintf(stderr, "Failed to write the cave images into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const terrain::CaveSlice slice = terrain::defaultCaveSlice(region);
    std::printf("seed %llu, center (%.0f, %.0f) m, %d x %d px, %.1f km wide, %zu entrances, "
                "caves under %.1f%% of the map; slice %d x %d px at %.2f m/px: "
                "%s/cave_slice.png, %s/cave_preview.png\n",
                static_cast<unsigned long long>(seed), area.centerX, area.centerZ, area.size,
                area.size, area.size * area.metersPerPixel / 1000.0, stats.entrances,
                100.0 * stats.cavedFraction, slice.width, slice.height, slice.metersPerPixel,
                argv[1], argv[1]);
    return EXIT_SUCCESS;
}
