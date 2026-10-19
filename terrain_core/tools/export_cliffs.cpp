// Writes cliffs_mask.png and cliffs_preview.png for visual checks of the cliffs, plateaus and
// gorges (square region centered on the cliff site of the world, see findCliffRegion).
// Usage: terrain_export_cliffs <output-dir> [seed] [meters-per-pixel] [size]

#include "terrain/generation/cliff_region.hpp"
#include "terrain/io/cliffs_png.hpp"

#include <charconv>
#include <cstddef>
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

// Share of the land samples where the cliff mask is 1 (full cliffs), in percent.
double fullMaskPercent(const terrain::CliffRegion& cliffs) {
    const terrain::HeightGrid& grid = cliffs.eroded().grid();
    std::size_t land = 0;
    std::size_t full = 0;
    for (std::size_t i = 0; i < grid.heights.size(); ++i) {
        if (grid.heights[i] >= terrain::kSeaLevel) {
            ++land;
            full += cliffs.mask()[i] >= 1.0f ? 1 : 0;
        }
    }
    return land == 0 ? 0.0 : 100.0 * static_cast<double>(full) / static_cast<double>(land);
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    // 6.4 km around the cliff site, as in the viewer, sampled finely enough to show the rims.
    double metersPerPixel = 12.5;
    int size = 512;
    const bool valid = (argc >= 2 && argc <= 5) && (argc < 3 || parse(argv[2], seed)) &&
                       (argc < 4 || (parse(argv[3], metersPerPixel) && metersPerPixel > 0.0)) &&
                       (argc < 5 || (parse(argv[4], size) && size >= 2));
    if (!valid) {
        std::fprintf(stderr, "Usage: %s <output-dir> [seed] [meters-per-pixel] [size]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }

    const terrain::HeightmapRegion region = terrain::findCliffRegion(seed, metersPerPixel, size);
    const terrain::CliffRegion cliffs(seed, region);
    if (!terrain::exportCliffPngs(cliffs, argv[1])) {
        std::fprintf(stderr, "Failed to write the cliff images into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    int riverCells = 0;
    for (const std::uint8_t river : cliffs.eroded().rivers().river) {
        riverCells += river;
    }
    const double extentKm = region.size * region.metersPerPixel / 1000.0;
    std::printf("seed %llu, center (%.0f, %.0f) m, %d x %d px, %.1f km wide, mask 1 on %.1f%% of "
                "the land, %d river cells, %zu cascades, %zu cirques: %s/cliffs_mask.png, "
                "%s/cliffs_preview.png\n",
                static_cast<unsigned long long>(seed), region.centerX, region.centerZ,
                region.size, region.size, extentKm, fullMaskPercent(cliffs), riverCells,
                cliffs.eroded().rivers().cascades.size(), cliffs.cirques().size(), argv[1],
                argv[1]);
    return EXIT_SUCCESS;
}
