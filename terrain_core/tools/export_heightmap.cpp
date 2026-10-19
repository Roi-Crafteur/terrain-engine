// Writes heightmap.png and heightmap_preview.png (1024 x 1024, centered on the origin) for
// visual checks of the Heightfield.
// Usage: terrain_export_heightmap <output-dir> [seed] [meters-per-pixel]

#include "terrain/generation/heightfield.hpp"
#include "terrain/io/heightmap_png.hpp"

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
    terrain::HeightmapRegion region;
    const bool valid = (argc >= 2 && argc <= 4) && (argc < 3 || parse(argv[2], seed)) &&
                       (argc < 4 || (parse(argv[3], region.metersPerPixel) &&
                                     region.metersPerPixel > 0.0));
    if (!valid) {
        std::fprintf(stderr, "Usage: %s <output-dir> [seed] [meters-per-pixel]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const std::filesystem::path directory = argv[1];
    const terrain::Heightfield field(seed);
    if (!terrain::exportHeightmapPngs(field, region, directory)) {
        std::fprintf(stderr, "Failed to write the heightmaps into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const double extentKm = region.size * region.metersPerPixel / 1000.0;
    std::printf("seed %llu, %d x %d px, %.1f km wide: %s/heightmap.png, %s/heightmap_preview.png\n",
                static_cast<unsigned long long>(seed), region.size, region.size, extentKm,
                argv[1], argv[1]);
    return EXIT_SUCCESS;
}
