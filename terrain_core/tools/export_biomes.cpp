// Writes biome_map.png, surface_material_map.png and vegetation_density_map.png for visual
// checks of the climate, the biomes, the surface materials and the vegetation laid on an
// eroded region (rivers included), square and centered on (center-x, center-z).
// Usage: terrain_export_biomes <output-dir> [seed] [meters-per-pixel] [size] [center-x]
//                              [center-z]
// Rivers start where the catchment reaches RiverParams::minCatchmentArea (2 km^2): beyond about
// 1.4 km per pixel every land cell is a river, and the material map shows water everywhere.

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/io/ecology_png.hpp"

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

template <class T>
bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

bool parseArguments(int argc, char** argv, std::uint64_t& seed, terrain::HeightmapRegion& region) {
    return (argc >= 2 && argc <= 7) && (argc < 3 || parse(argv[2], seed)) &&
           (argc < 4 || (parse(argv[3], region.metersPerPixel) && region.metersPerPixel > 0.0 &&
                         std::isfinite(region.metersPerPixel))) &&
           (argc < 5 || (parse(argv[4], region.size) && region.size >= 2)) &&
           (argc < 6 || (parse(argv[5], region.centerX) && std::isfinite(region.centerX))) &&
           (argc < 7 || (parse(argv[6], region.centerZ) && std::isfinite(region.centerZ)));
}

// Share of the samples of each biome, printed for those present.
void printBiomes(const terrain::EcologyMap& map) {
    std::array<std::size_t, terrain::kBiomeCount> counts{};
    for (const terrain::EcologySample& sample : map.samples) {
        ++counts[static_cast<std::size_t>(sample.biome)];
    }
    int present = 0;
    for (const std::size_t count : counts) {
        present += count > 0 ? 1 : 0;
    }
    std::printf("%d biomes:", present);
    const char* separator = " ";
    for (int b = 0; b < terrain::kBiomeCount; ++b) {
        const std::size_t count = counts[static_cast<std::size_t>(b)];
        if (count > 0) {
            const double share =
                100.0 * static_cast<double>(count) / static_cast<double>(map.samples.size());
            std::printf("%s%s %.1f%%", separator,
                        terrain::biomeName(static_cast<terrain::Biome>(b)), share);
            separator = ", ";
        }
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    // 102.4 km around the origin: several climates, the mountains and the rivers.
    terrain::HeightmapRegion region{.metersPerPixel = 200.0, .size = 512};
    if (!parseArguments(argc, argv, seed, region)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [seed] [meters-per-pixel] [size] [center-x] "
                     "[center-z]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }

    const terrain::ErodedRegion eroded(seed, region);
    const terrain::Ecology ecology(seed);
    const terrain::EcologyMap map = ecology.map(terrain::surfaceFromEroded(eroded));
    if (!terrain::exportEcologyPngs(map, argv[1])) {
        std::fprintf(stderr, "Failed to write the biome maps into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    std::printf("seed %llu, %d x %d px, %.1f km wide, center (%.0f, %.0f): %s/biome_map.png, "
                "%s/surface_material_map.png, %s/vegetation_density_map.png\n",
                static_cast<unsigned long long>(seed), region.size, region.size,
                region.size * region.metersPerPixel / 1000.0, region.centerX, region.centerZ,
                argv[1], argv[1], argv[1]);
    printBiomes(map);
    return EXIT_SUCCESS;
}
