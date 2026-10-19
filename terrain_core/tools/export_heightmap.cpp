// Writes heightmap.png and heightmap_preview.png for visual checks of the Heightfield: a square
// of size x size pixels, meters-per-pixel apart, centered on (center-x, center-z). Without the
// optional arguments: seed 20261019, 100 m/px, 1024 px, centered on the origin, the default
// macro continent amplitude (0 disables the planetary layer of large oceans and continents).
// Usage: terrain_export_heightmap <output-dir> [seed] [meters-per-pixel] [size] [center-x]
//        [center-z] [macro-continent]

#include "terrain/generation/heightfield.hpp"
#include "terrain/io/heightmap_png.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

// Image sizes accepted, in pixels per side.
constexpr int kMinSize = 2;
constexpr int kMaxSize = 16384;

template <class T>
bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

// A finite number of meters; negative values are fine.
bool parseCoordinate(const char* text, double& value, const char* name) {
    if (parse(text, value) && std::isfinite(value)) {
        return true;
    }
    std::fprintf(stderr, "Invalid %s '%s': expected a number of meters\n", name, text);
    return false;
}

// A finite amplitude of at least 0.
bool parseMacroContinent(const char* text, float& value) {
    if (parse(text, value) && std::isfinite(value) && value >= 0.0f) {
        return true;
    }
    std::fprintf(stderr, "Invalid macro continent '%s': expected a number of at least 0\n", text);
    return false;
}

struct Options {
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::HeightmapRegion region; // 1024 px at 100 m/px around the origin by default
    terrain::HeightfieldParams params;
};

// Reads the optional arguments into options. On the first invalid one, prints what is wrong
// and returns false; nothing has been generated or written then.
bool parseArguments(int argc, char** argv, Options& options) {
    if (argc < 2 || argc > 8) {
        std::fprintf(stderr, "Expected 1 to 7 arguments, got %d\n", argc - 1);
        return false;
    }
    terrain::HeightmapRegion& region = options.region;
    if (argc > 2 && !parse(argv[2], options.seed)) {
        std::fprintf(stderr, "Invalid seed '%s': expected an unsigned integer\n", argv[2]);
        return false;
    }
    if (argc > 3 && !(parse(argv[3], region.metersPerPixel) &&
                      std::isfinite(region.metersPerPixel) && region.metersPerPixel > 0.0)) {
        std::fprintf(stderr, "Invalid meters per pixel '%s': expected a number above 0\n",
                     argv[3]);
        return false;
    }
    if (argc > 4 &&
        !(parse(argv[4], region.size) && region.size >= kMinSize && region.size <= kMaxSize)) {
        std::fprintf(stderr, "Invalid size '%s': expected an integer from %d to %d pixels\n",
                     argv[4], kMinSize, kMaxSize);
        return false;
    }
    return (argc <= 5 || parseCoordinate(argv[5], region.centerX, "center x")) &&
           (argc <= 6 || parseCoordinate(argv[6], region.centerZ, "center z")) &&
           (argc <= 7 || parseMacroContinent(argv[7], options.params.macroContinent));
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArguments(argc, argv, options)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [seed] [meters-per-pixel] [size] [center-x] "
                     "[center-z] [macro-continent]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }

    const std::filesystem::path directory = argv[1];
    const terrain::HeightmapRegion& region = options.region;
    const terrain::Heightfield field(options.seed, options.params);
    if (!terrain::exportHeightmapPngs(field, region, directory)) {
        std::fprintf(stderr, "Failed to write the heightmaps into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const double extentKm = region.size * region.metersPerPixel / 1000.0;
    std::printf("seed %llu, %d x %d px, %.1f km wide, center (%.0f, %.0f), macro continent %.2f: "
                "%s/heightmap.png, %s/heightmap_preview.png\n",
                static_cast<unsigned long long>(options.seed), region.size, region.size,
                extentKm, region.centerX, region.centerZ,
                static_cast<double>(options.params.macroContinent), argv[1], argv[1]);
    return EXIT_SUCCESS;
}
