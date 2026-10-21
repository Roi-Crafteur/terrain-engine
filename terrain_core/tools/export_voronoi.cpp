// Writes the PNGs of the tectonic plate layer for visual checks: voronoi_plates.png,
// voronoi_heightmap.png and voronoi_preview.png of a square of size x size pixels,
// meters-per-pixel apart, centered on (center-x, center-z), and voronoi_world.png, the whole
// planet in world-size x world-size pixels. Without the optional arguments: seed 20261019,
// 8000 m/px, 4096 px (32,768 km) around the origin, plates 0.5, macro continent 0.25 and a
// 2048 px world map.
// Usage: terrain_export_voronoi <output-dir> [seed] [meters-per-pixel] [size] [center-x]
//        [center-z] [plates] [macro-continent] [world-size]

#include "terrain/generation/heightfield.hpp"
#include "terrain/io/voronoi_png.hpp"

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

bool parseSize(const char* text, int& value, const char* name) {
    if (parse(text, value) && value >= kMinSize && value <= kMaxSize) {
        return true;
    }
    std::fprintf(stderr, "Invalid %s '%s': expected an integer from %d to %d pixels\n", name, text,
                 kMinSize, kMaxSize);
    return false;
}

// A finite number of meters; negative values are fine.
bool parseCoordinate(const char* text, double& value, const char* name) {
    if (parse(text, value) && std::isfinite(value)) {
        return true;
    }
    std::fprintf(stderr, "Invalid %s '%s': expected a number of meters\n", name, text);
    return false;
}

// A finite amplitude, above 0 when the layer must be enabled, at least 0 otherwise.
bool parseAmplitude(const char* text, float& value, const char* name, bool positive) {
    if (parse(text, value) && std::isfinite(value) && (positive ? value > 0.0f : value >= 0.0f)) {
        return true;
    }
    std::fprintf(stderr, "Invalid %s '%s': expected a number %s 0\n", name, text,
                 positive ? "above" : "of at least");
    return false;
}

struct Options {
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::HeightmapRegion region{0.0, 0.0, 8000.0, 4096};
    terrain::HeightfieldParams params;
    int worldSize = 2048;
};

// Reads the optional arguments into options. On the first invalid one, prints what is wrong
// and returns false; nothing has been generated or written then.
bool parseArguments(int argc, char** argv, Options& options) {
    if (argc < 2 || argc > 10) {
        std::fprintf(stderr, "Expected 1 to 9 arguments, got %d\n", argc - 1);
        return false;
    }
    terrain::HeightmapRegion& region = options.region;
    if (argc > 2 && !parse(argv[2], options.seed)) {
        std::fprintf(stderr, "Invalid seed '%s': expected an unsigned integer\n", argv[2]);
        return false;
    }
    if (argc > 3 && !(parse(argv[3], region.metersPerPixel) &&
                      std::isfinite(region.metersPerPixel) && region.metersPerPixel > 0.0)) {
        std::fprintf(stderr, "Invalid meters per pixel '%s': expected a number above 0\n", argv[3]);
        return false;
    }
    return (argc <= 4 || parseSize(argv[4], region.size, "size")) &&
           (argc <= 5 || parseCoordinate(argv[5], region.centerX, "center x")) &&
           (argc <= 6 || parseCoordinate(argv[6], region.centerZ, "center z")) &&
           (argc <= 7 ||
            parseAmplitude(argv[7], options.params.plates.amplitude, "plates", true)) &&
           (argc <= 8 ||
            parseAmplitude(argv[8], options.params.macroContinent, "macro continent", false)) &&
           (argc <= 9 || parseSize(argv[9], options.worldSize, "world size"));
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    options.params.plates.amplitude = 0.5f;
    options.params.macroContinent = 0.25f;
    if (!parseArguments(argc, argv, options)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [seed] [meters-per-pixel] [size] [center-x] "
                     "[center-z] [plates] [macro-continent] [world-size]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }

    const std::filesystem::path directory = argv[1];
    const terrain::HeightmapRegion& region = options.region;
    const terrain::Heightfield field(options.seed, options.params);
    if (!terrain::exportVoronoiPngs(field, region, options.worldSize, directory)) {
        std::fprintf(stderr, "Failed to write the Voronoi maps into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const terrain::TectonicPlates& plates = *field.plates();
    const double extentKm = region.size * region.metersPerPixel / 1000.0;
    std::printf("seed %llu, %zu plates, world %.0f x %.0f km, %d x %d px, %.1f "
                "km wide, center "
                "(%.0f, %.0f), plates %.2f, macro continent %.2f, world %d px: "
                "%s/voronoi_plates.png, %s/voronoi_heightmap.png, "
                "%s/voronoi_preview.png, "
                "%s/voronoi_world.png\n",
                static_cast<unsigned long long>(options.seed), plates.plates().size(),
                plates.world().x / 1000.0, plates.world().z / 1000.0, region.size, region.size,
                extentKm, region.centerX, region.centerZ,
                static_cast<double>(options.params.plates.amplitude),
                static_cast<double>(options.params.macroContinent), options.worldSize, argv[1],
                argv[1], argv[1], argv[1]);
    return EXIT_SUCCESS;
}
