// Maps and figures of a relief, to compare reliefs and judge them: the whole planet in
// hypsometric colors shaded (world.png), zooms of 2,048 km, 200 km and 20 km on a mountain
// chain, a plain and a coast found on the planet (zoom_<site>_<km>km.png), and the figures of
// terrain::measureRelief and measureLocalRelief (printed, and written to stats.txt). Without
// the optional arguments: seed 20261019, the default relief, cells of 16,384 m for the planet,
// zooms of 2,048 px, all the hardware threads, the default world.
// Usage: terrain_export_relief <output-dir> [seed] [default|planetary|legacy|geography] [cell-size]
//        [zoom-pixels] [threads] [world-x] [world-z]

#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/io/relief_png.hpp"
#include "terrain/io/relief_stats.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

constexpr int kMaxMapWidth = 4096;
constexpr int kWindowCount = 64; // walking-scale windows of each kind

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

struct Options {
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    std::string relief = "default";
    double cellSize = 16384.0;
    int zoomPixels = 2048;
    int threads = 0;
    terrain::WorldSize world;
};

bool fail(const char* what, const char* text, const char* expected) {
    std::fprintf(stderr, "Invalid %s '%s': expected %s\n", what, text, expected);
    return false;
}

// default: HeightfieldParams{}; planetary: the preset of the planet level; legacy: the relief
// of generator 2 (geography disabled); geography: the planetary geography enabled.
terrain::HeightfieldParams reliefParams(const std::string& relief) {
    terrain::HeightfieldParams params = relief == "planetary"
                                            ? terrain::planetaryHeightfieldParams()
                                            : terrain::HeightfieldParams{};
    if (relief == "legacy" || relief == "geography") {
        params.geography.enabled = relief == "geography";
    }
    return params;
}

bool parseWorld(int argc, char** argv, Options& options) {
    if (argc > 7 && !(parse(argv[7], options.world.x) && std::isfinite(options.world.x))) {
        return fail("world x", argv[7], "a number of meters");
    }
    if (argc > 8 && !(parse(argv[8], options.world.z) && std::isfinite(options.world.z))) {
        return fail("world z", argv[8], "a number of meters");
    }
    try {
        terrain::validate(options.world);
    } catch (const std::invalid_argument&) {
        return fail("world size", argc > 7 ? argv[7] : "default", "multiples of 65,536 m");
    }
    const double cells = std::min(options.world.x, options.world.z) / options.cellSize;
    if (std::fmod(options.world.x, options.cellSize) != 0.0 ||
        std::fmod(options.world.z, options.cellSize) != 0.0 || cells < 3.0) {
        return fail("cell size", argc > 4 ? argv[4] : "16384",
                    "meters dividing both sides of the world, at least 3 cells per side");
    }
    return true;
}

// Reads the optional arguments into options. On the first invalid one, prints what is wrong
// and returns false; nothing has been computed or written then.
bool parseArguments(int argc, char** argv, Options& options) {
    if (argc < 2 || argc > 9) {
        std::fprintf(stderr, "Expected 1 to 8 arguments, got %d\n", argc - 1);
        return false;
    }
    if (argc > 2 && !parse(argv[2], options.seed)) {
        return fail("seed", argv[2], "an unsigned integer");
    }
    if (argc > 3) {
        options.relief = argv[3];
        if (options.relief != "default" && options.relief != "planetary" &&
            options.relief != "legacy" && options.relief != "geography") {
            return fail("relief", argv[3], "default, planetary, legacy or geography");
        }
    }
    if (argc > 4 && !(parse(argv[4], options.cellSize) && std::isfinite(options.cellSize) &&
                      options.cellSize > 0.0)) {
        return fail("cell size", argv[4], "a number of meters above 0");
    }
    if (argc > 5 && !(parse(argv[5], options.zoomPixels) && options.zoomPixels >= 16 &&
                      options.zoomPixels <= 8192)) {
        return fail("zoom pixels", argv[5], "an integer from 16 to 8192");
    }
    if (argc > 6 && !(parse(argv[6], options.threads) && options.threads >= 0)) {
        return fail("threads", argv[6], "an integer of at least 0 (0: all the hardware threads)");
    }
    return parseWorld(argc, argv, options);
}

// What the tool prints, also kept for stats.txt.
class Report {
public:
    void add(const char* format, ...) {
        std::array<char, 1024> line{};
        va_list args;
        va_start(args, format);
        std::vsnprintf(line.data(), line.size(), format, args);
        va_end(args);
        std::fputs(line.data(), stdout);
        text_ += line.data();
    }
    [[nodiscard]] bool write(const std::filesystem::path& path) const {
        std::FILE* file = std::fopen(path.string().c_str(), "wb");
        if (file == nullptr) {
            return false;
        }
        const bool written = std::fwrite(text_.data(), 1, text_.size(), file) == text_.size();
        return std::fclose(file) == 0 && written;
    }

private:
    std::string text_;
};

double seconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

void reportLand(Report& r, const terrain::ReliefStats& s) {
    r.add("land: %.1f%% of the planet, %zu land masses, %d continents of at least 10,000,000 "
          "km2\nlargest land masses (km2):",
          100.0 * s.landShare, s.landMasses.size(), s.continents);
    for (std::size_t k = 0; k < s.landMasses.size() && k < 10; ++k) {
        r.add(" %.0f", s.landMasses[k].area / 1.0e6);
    }
    r.add("\naltitudes of the land:");
    float low = 0.0f;
    for (std::size_t b = 0; b < s.altitudeShares.size(); ++b) {
        if (b < terrain::kAltitudeBands.size()) {
            r.add(" %.0f-%.0f m %.1f%% |", static_cast<double>(low),
                  static_cast<double>(terrain::kAltitudeBands[b]), 100.0 * s.altitudeShares[b]);
            low = terrain::kAltitudeBands[b];
        } else {
            r.add(" above %.0f m %.1f%%\n", static_cast<double>(low), 100.0 * s.altitudeShares[b]);
        }
    }
    r.add("highest: %.0f m at (%.0f, %.0f) km\n", static_cast<double>(s.highest),
          s.highestX / 1000.0, s.highestZ / 1000.0);
}

void printStraight(Report& r, const char* what, const terrain::StraightCoast& c,
                   const terrain::WorldSize& world) {
    r.add("%s: longest straight stretch %.0f km, from (%.0f, %.0f) to (%.0f, %.0f) km\n", what,
          c.length / 1000.0, terrain::wrapCoordinate(c.x0, world.x) / 1000.0,
          terrain::wrapCoordinate(c.z0, world.z) / 1000.0,
          terrain::wrapCoordinate(c.x1, world.x) / 1000.0,
          terrain::wrapCoordinate(c.z1, world.z) / 1000.0);
}

void reportShapes(Report& r, const terrain::ReliefStats& s, const terrain::WorldSize& world) {
    r.add("chains: %zu; mountain cells %.1f%% of the land, chains %.1f%% of the land\n",
          s.chains.size(), 100.0 * s.mountainShare, 100.0 * s.chainShare);
    for (std::size_t k = 0; k < s.chains.size() && k < 5; ++k) {
        const terrain::MountainChain& c = s.chains[k];
        r.add("chain %zu: %.0f km long, %.0f km wide, %.0f km2, highest %.0f m at (%.0f, %.0f) "
              "km\n",
              k + 1, c.length / 1000.0, c.width / 1000.0, c.area / 1.0e6,
              static_cast<double>(c.highest), c.x / 1000.0, c.z / 1000.0);
    }
    r.add("plains (over 500 km from every chain): %.1f%% of the land, %.2f%% of them above "
          "1,500 m",
          100.0 * s.plainShare, 100.0 * s.highPlainShare);
    if (s.chains.empty()) {
        r.add("; no chain\n");
    } else {
        r.add("; the land farthest from the chains is %.0f km away\n",
              s.plainSiteDistance / 1000.0);
    }
    printStraight(r, "coasts", s.longestStraightCoast, world);
    printStraight(r, "coasts smoothed over about 160 km", s.longestStraightMapCoast, world);
    r.add("coast length at the grid resolution: %.0f km\n", s.coastLength / 1000.0);
}
void reportLocal(Report& r, const terrain::LocalReliefStats& l) {
    r.add("walk, 1 km steps over 10 km: plains slope %.2f%%, relief %.0f m (%d windows); "
          "mountains relief %.0f m, 90th percentile %.0f m (%d windows)\n",
          100.0 * l.plainSlope, l.plainRelief, l.plainWindows, l.mountainRelief,
          l.mountainRelief90, l.mountainWindows);
    r.add("coast sinuosity, length with 1 km dividers / with 10 km dividers: %.3f (median of "
          "%d windows of 40 km)\n",
          l.coastSinuosity, l.coastWindows);
}

// The zooms of 2,048, 200 and 20 km around a site.
bool writeZooms(const terrain::Heightfield& field, const Options& options, const char* name,
                const terrain::ReliefSite& site, const std::filesystem::path& directory,
                Report& r) {
    if (!site.found) {
        r.add("no %s found: no zoom\n", name);
        return true;
    }
    r.add("%s at (%.0f, %.0f) km:", name, site.x / 1000.0, site.z / 1000.0);
    for (const int km : {2048, 200, 20}) {
        const double spacing = km * 1000.0 / options.zoomPixels;
        const terrain::ReliefGrid grid =
            terrain::sampleReliefWindow(field, site.x, site.z, spacing, options.zoomPixels,
                                        options.zoomPixels, options.threads);
        const std::string file = "zoom_" + std::string(name) + "_" + std::to_string(km) + "km.png";
        if (!terrain::writePng(directory / file, terrain::renderRelief(grid))) {
            return false;
        }
        r.add(" %s", file.c_str());
    }
    r.add("\n");
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArguments(argc, argv, options)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [seed] [default|planetary|legacy|geography] "
                     "[cell-size] "
                     "[zoom-pixels] [threads] [world-x] [world-z]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    const std::filesystem::path directory = argv[1];
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const terrain::Heightfield field(options.seed, reliefParams(options.relief), options.world);
    auto start = std::chrono::steady_clock::now();
    const terrain::ReliefGrid world =
        terrain::sampleReliefWorld(field, options.cellSize, options.threads);
    const double sampling = seconds(start);
    start = std::chrono::steady_clock::now();
    const terrain::ReliefStats stats = terrain::measureRelief(world);
    const terrain::LocalReliefStats local =
        terrain::measureLocalRelief(field, world, stats, kWindowCount, options.threads);
    Report r;
    r.add("relief %s, seed %llu, world %.0f x %.0f km, cells of %.0f m (%d x %d): sampled in "
          "%.1f s, measured in %.1f s\n",
          options.relief.c_str(), static_cast<unsigned long long>(options.seed),
          options.world.x / 1000.0, options.world.z / 1000.0, options.cellSize, world.columns,
          world.rows, sampling, seconds(start));
    reportLand(r, stats);
    reportShapes(r, stats, options.world);
    reportLocal(r, local);
    const int factor = (world.columns + kMaxMapWidth - 1) / kMaxMapWidth;
    const bool mapWritten = terrain::writePng(
        directory / "world.png", terrain::renderRelief(terrain::downsampleRelief(world, factor)));
    const bool zooms = writeZooms(field, options, "mountain", stats.mountain, directory, r) &&
                       writeZooms(field, options, "plain", stats.plain, directory, r) &&
                       writeZooms(field, options, "coast", stats.coast, directory, r);
    if (!mapWritten || !zooms || !r.write(directory / "stats.txt")) {
        std::fprintf(stderr, "Failed to write the maps into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
