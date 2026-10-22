// Maps and figures of a relief, to compare reliefs and judge them: the whole planet in
// hypsometric colors shaded (world.png), zooms of 2,048 km, 200 km and 20 km on a mountain
// chain, a plain and a coast found on the planet (zoom_<site>_<km>km.png) and on the places
// given by --at (fixed places, to compare reliefs at the same points), and the figures of
// terrain::measureRelief, measureLocalRelief, measureTerrain and measurePeaks (printed, and
// written to stats.txt). --without ravines or --without lively switches off a step of the relief of the
// geography (GeographyParams::ravines, livelyPlains), to compare them. Without the optional
// arguments: seed 20261019, the default relief, cells of 16,384 m for the planet, zooms of
// 2,048 px, all the hardware threads, the default world.
// Usage: terrain_export_relief <output-dir> [seed] [default|planetary|legacy|geography] [cell-size]
//        [zoom-pixels] [threads] [world-x] [world-z] [--at <name> <x> <z>]...
//        [--without ravines|lively]...

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
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr int kMaxMapWidth = 4096;
constexpr int kWindowCount = 64; // walking-scale windows of each kind

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

// A place given by --at: its name (letters, digits and _) and its point, meters.
struct Place {
    std::string name;
    double x = 0.0;
    double z = 0.0;
};

struct Options {
    std::vector<Place> places;
    bool ravines = true; // GeographyParams::ravines, livelyPlains
    bool lively = true;
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

// default: HeightfieldParams{}, the planetary geography since generator 3; planetary: the preset
// of the planet level (the same world); legacy: the relief of generator 2 (geography disabled);
// geography: the planetary geography enabled.
terrain::HeightfieldParams reliefParams(const Options& options) {
    terrain::HeightfieldParams params = options.relief == "planetary"
                                            ? terrain::planetaryHeightfieldParams()
                                            : terrain::HeightfieldParams{};
    if (options.relief == "legacy" || options.relief == "geography") {
        params.geography.enabled = options.relief == "geography";
    }
    params.geography.ravines = options.ravines;
    params.geography.livelyPlains = options.lively;
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

bool validName(const std::string& name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    });
}

// Reads --without <step> at argv[k] into options.
bool parseWithout(int argc, char** argv, int k, Options& options) {
    const bool ravines = k + 1 < argc && std::strcmp(argv[k + 1], "ravines") == 0;
    const bool lively = k + 1 < argc && std::strcmp(argv[k + 1], "lively") == 0;
    if (!ravines && !lively) {
        std::fprintf(stderr, "Invalid step: expected --without ravines or lively\n");
        return false;
    }
    options.ravines = options.ravines && !ravines;
    options.lively = options.lively && !lively;
    return true;
}

// Moves the --at triples and --without <step> out of the arguments into options; the other
// arguments stay in order in kept (the program name first).
bool parsePlaces(int argc, char** argv, Options& options, std::vector<char*>& kept) {
    for (int k = 0; k < argc; ++k) {
        if (std::strcmp(argv[k], "--without") == 0) {
            if (!parseWithout(argc, argv, k, options)) {
                return false;
            }
            ++k;
            continue;
        }
        if (std::strcmp(argv[k], "--at") != 0) {
            kept.push_back(argv[k]);
            continue;
        }
        if (k + 3 >= argc) {
            std::fprintf(stderr, "Invalid place: expected --at <name> <x> <z>\n");
            return false;
        }
        Place place{argv[k + 1], 0.0, 0.0};
        if (!validName(place.name)) {
            return fail("place name", argv[k + 1], "letters, digits and _");
        }
        if (!(parse(argv[k + 2], place.x) && std::isfinite(place.x))) {
            return fail("place x", argv[k + 2], "a number of meters");
        }
        if (!(parse(argv[k + 3], place.z) && std::isfinite(place.z))) {
            return fail("place z", argv[k + 3], "a number of meters");
        }
        options.places.push_back(place);
        k += 3;
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
        std::ofstream file(path, std::ios::binary);
        file.write(text_.data(), static_cast<std::streamsize>(text_.size()));
        file.close();
        return static_cast<bool>(file);
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
    if (l.beltWindows > 0) {
        r.add("walk in the mountain belts of the geography, 1 km steps over 10 km: relief %.0f m, "
              "90th percentile %.0f m (%d windows)\n",
              l.beltRelief, l.beltRelief90, l.beltWindows);
    }
    r.add("coast sinuosity, length with 1 km dividers / with 10 km dividers: %.3f (median of "
          "%d windows of 40 km)\n",
          l.coastSinuosity, l.coastWindows);
}

void printZone(Report& r, const char* what, double share, const terrain::ReliefZone& zone) {
    r.add("%s: %.1f%%; widest zone %.0f km wide (%.0f km2) around (%.0f, %.0f) km\n", what,
          100.0 * share, zone.width / 1000.0, zone.area / 1.0e6, zone.x / 1000.0,
          zone.z / 1000.0);
}

void reportTerrain(Report& r, const terrain::TerrainFigures& f, bool geography) {
    const char* belts = geography ? "belts" : "mountain cells";
    r.add("%s (%d windows): relief over 5 km %.0f m (median); slopes at 25 m above 30 degrees "
          "%.1f%%, above 45 degrees %.2f%%\n",
          belts, f.beltWindows, f.beltRelief5, 100.0 * f.steepShare, 100.0 * f.cliffShare);
    r.add("plains (%d windows): relief over 10 km min %.0f m, 10th percentile %.0f m, median "
          "%.0f m, 90th percentile %.0f m; windows below 10 m %.1f%%; relief over 1 km 10th "
          "percentile %.0f m, median %.0f m\n",
          f.plainWindows, f.plainReliefMin, f.plainRelief10, f.plainRelief50, f.plainRelief90,
          100.0 * f.plainFlatShare, f.plainNear10, f.plainNear50);
    r.add("land below the mean within 1 km (%d windows): %.1f%%; plains %.1f%%, %s %.1f%%\n",
          f.meanWindows, 100.0 * f.belowMean, 100.0 * f.belowMeanPlains, belts,
          100.0 * f.belowMeanBelts);
    printZone(r, "land with less than 10 m of relief over 10 km", f.flatShare, f.widestFlat);
    printZone(r, "highlands (belts or 2,500 m) with less than 300 m of relief over 10 km",
              f.dullShare, f.widestDull);
    if (geography) {
        r.add("land by intensity of the relief: below %.2f %.1f%%",
              static_cast<double>(terrain::kReliefClasses[0]), 100.0 * f.reliefShares[0]);
        for (std::size_t c = 1; c < terrain::kReliefClasses.size(); ++c) {
            r.add(" | %.2f-%.2f %.1f%%", static_cast<double>(terrain::kReliefClasses[c - 1]),
                  static_cast<double>(terrain::kReliefClasses[c]), 100.0 * f.reliefShares[c]);
        }
        r.add(" | above %.2f %.1f%%\n", static_cast<double>(terrain::kReliefClasses.back()),
              100.0 * f.reliefShares.back());
    }
}

void reportPeaks(Report& r, const terrain::PeakFigures& p) {
    if (p.cores == 0) {
        return;
    }
    r.add("cores of the chains (%d cells of intensity %.1f or more): slopes at 2 m above 45 "
          "degrees %.1f%%, above 70 degrees %.1f%%; their summits (highest %.0f m): dominance over "
          "3 to 5 km %.0f m (median), %.0f%% at least %.0f m; faces over their first 1,500 m of "
          "drop %.1f degrees (median), %.0f%% at least %.0f; from the floor 8 to 14 km away that "
          "sees them, %.1f degrees above the horizontal (median), %.0f%% at least %.0f; slope of "
          "those floors %.1f degrees (median)\n",
          p.cores, static_cast<double>(terrain::kCoreRelief), 100.0 * p.cliffShare,
          100.0 * p.wallShare, p.highest, p.dominance50, 100.0 * p.dominantShare,
          terrain::kDominant, p.faces50, 100.0 * p.steepFaces, terrain::kSteepFaces, p.seen50,
          100.0 * p.seenShare, terrain::kSeenAngle, p.floorSlope50);
}

// The point of the shore nearest to a coast site of the map, whose cells are too coarse to put
// it within the 20 km zoom: on a grid of 500 m over 64 km around it, the nearest sample with a
// neighbor on the other side of the sea level (the site itself when there is none).
terrain::ReliefSite onShore(const terrain::Heightfield& field, terrain::ReliefSite site) {
    constexpr int kHalf = 64;
    constexpr double kStep = 500.0;
    const auto land = [&](int i, int j) {
        return field.heightAt(site.x + i * kStep, site.z + j * kStep) >= terrain::kSeaLevel;
    };
    terrain::ReliefSite best = site;
    int nearest = 2 * kHalf * kHalf + 1;
    for (int j = -kHalf; j < kHalf; ++j) {
        for (int i = -kHalf; i < kHalf; ++i) {
            if (i * i + j * j < nearest && land(i, j) != land(i + 1, j)) {
                nearest = i * i + j * j;
                best = {true, site.x + (i + 0.5) * kStep, site.z + j * kStep};
            }
        }
    }
    return best;
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
    std::vector<char*> args;
    if (!parsePlaces(argc, argv, options, args) ||
        !parseArguments(static_cast<int>(args.size()), args.data(), options)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [seed] [default|planetary|legacy|geography] "
                     "[cell-size] [zoom-pixels] [threads] [world-x] [world-z] "
                     "[--at <name> <x> <z>]... [--without ravines|lively]...\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    const std::filesystem::path directory = args[1];
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const terrain::Heightfield field(options.seed, reliefParams(options), options.world);
    auto start = std::chrono::steady_clock::now();
    const terrain::ReliefGrid world =
        terrain::sampleReliefWorld(field, options.cellSize, options.threads);
    const double sampling = seconds(start);
    start = std::chrono::steady_clock::now();
    const terrain::ReliefStats stats = terrain::measureRelief(world);
    const terrain::LocalReliefStats local =
        terrain::measureLocalRelief(field, world, stats, kWindowCount, options.threads);
    const terrain::TerrainFigures terrainFigures =
        terrain::measureTerrain(field, world, stats, kWindowCount, options.threads);
    const terrain::PeakFigures peaks =
        terrain::measurePeaks(field, world, stats, 4 * kWindowCount, options.threads);
    Report r;
    r.add("relief %s, seed %llu, world %.0f x %.0f km, cells of %.0f m (%d x %d): sampled in "
          "%.1f s, measured in %.1f s\n",
          options.relief.c_str(), static_cast<unsigned long long>(options.seed),
          options.world.x / 1000.0, options.world.z / 1000.0, options.cellSize, world.columns,
          world.rows, sampling, seconds(start));
    reportLand(r, stats);
    reportShapes(r, stats, options.world);
    reportLocal(r, local);
    reportTerrain(r, terrainFigures, field.geography() != nullptr);
    reportPeaks(r, peaks);
    const int factor = (world.columns + kMaxMapWidth - 1) / kMaxMapWidth;
    const bool mapWritten = terrain::writePng(
        directory / "world.png", terrain::renderRelief(terrain::downsampleRelief(world, factor)));
    const bool zooms = writeZooms(field, options, "mountain", stats.mountain, directory, r) &&
                       writeZooms(field, options, "plain", stats.plain, directory, r) &&
                       writeZooms(field, options, "coast",
                                  stats.coast.found ? onShore(field, stats.coast) : stats.coast,
                                  directory, r);
    bool places = true;
    for (const Place& place : options.places) {
        places = places && writeZooms(field, options, place.name.c_str(),
                                      {true, place.x, place.z}, directory, r);
    }
    if (!mapWritten || !zooms || !places || !r.write(directory / "stats.txt")) {
        std::fprintf(stderr, "Failed to write the maps into %s\n", args[1]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
