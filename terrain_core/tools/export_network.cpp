// Maps of the hydrographic network (task 11C, step 1): around each place, maps of 2,048, 200 and
// 20 km (network_<name>_<km>km.png) of the streams over the shaded relief, colored by their order
// of Strahler (light to dark: 1 to 8), their width growing with their discharge (a logarithmic
// scale); at 2,048 km the network of the planet level (its links of 16 cells or 10 m^3/s at least)
// and its ends (sea: navy squares, terminal lakes: cyan discs, dry basins: brown discs, mouths in a
// lake: cyan rings, frozen ground: white discs; the dry ones, wadis, brown and dashed); at 200 km the trunks and the channels of order 3
// and more of the fine network; at 20 km every channel (dry ones dashed), the sources (black dots)
// and the confluences (white rings). And basins_<name>_2048km.png: the basins of the planet level,
// their divides in black. Prints, and writes to network.txt, what each map holds and what it cost.
// Usage: terrain_export_network <output-dir> --planet <file> [--threads <n>] [--pixels <n>]
//        [--place <name> <x> <z>]...

#include "network_draw.hpp"

#include "terrain/generation/water/drainage_network.hpp"
#include "terrain/io/relief_png.hpp"
#include "terrain/storage/planet_file.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using network_tool::Canvas;
using network_tool::Rgb;

struct Place {
    std::string name;
    double x = 0.0;
    double z = 0.0;
};

struct Options {
    std::filesystem::path directory;
    std::filesystem::path planet;
    int threads = 0;
    int pixels = 1024;
    std::vector<Place> places;
};

// The places of the maps without --place: a wet mountain (the fall of 388 m), a plain (the river
// of the plain), a desert (the wall of the giant waves of agent 34, 44 mm of rain a year), the K2.
std::vector<Place> defaultPlaces() {
    return {{"montagne_humide", -6'185'411.0, 156'218.0},
            {"plaine", -241'631.0, -86'277.0},
            {"desert", -323'111.0, 3'952'778.0},
            {"k2", 416'102.0, -11'414'891.0}};
}

bool parse(int argc, char** argv, Options& o) {
    if (argc < 2) {
        return false;
    }
    o.directory = argv[1];
    for (int a = 2; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--planet" && a + 1 < argc) {
            o.planet = argv[++a];
        } else if (arg == "--threads" && a + 1 < argc) {
            o.threads = std::atoi(argv[++a]);
        } else if (arg == "--pixels" && a + 1 < argc) {
            o.pixels = std::atoi(argv[++a]);
        } else if (arg == "--place" && a + 3 < argc) {
            o.places.push_back({argv[a + 1], std::atof(argv[a + 2]), std::atof(argv[a + 3])});
            a += 3;
        } else {
            return false;
        }
    }
    if (o.places.empty()) {
        o.places = defaultPlaces();
    }
    return !o.planet.empty() && o.pixels > 0;
}

} // namespace

namespace network_tool {
void planetMap(const terrain::PlanetNetwork& net, const Canvas& c, double half);
void basinsMap(const terrain::PlanetNetwork& net, terrain::Image& image, double x0, double z0,
               double spacing);
void fineMap(const terrain::DrainageNetwork& net, const Canvas& c, double half, bool all,
             std::FILE* report);
} // namespace network_tool

namespace {

// The maps of a place at one scale.
bool writeMap(const Options& o, const terrain::Heightfield& relief,
              const terrain::DrainageNetwork& net, const Place& p, double km, std::FILE* report) {
    const double spacing = km * 1000.0 / o.pixels;
    const auto t0 = std::chrono::steady_clock::now();
    terrain::Image image = terrain::renderRelief(
        terrain::sampleReliefWindow(relief, p.x, p.z, spacing, o.pixels, o.pixels, o.threads));
    network_tool::fade(image, 0.45);
    const double half = 0.5 * km * 1000.0;
    const Canvas canvas{&image, p.x - half, p.z - half, spacing};
    if (km > 1000.0) {
        network_tool::planetMap(net.planetNetwork(), canvas, half);
    } else {
        network_tool::fineMap(net, canvas, half, km < 100.0, report);
    }
    const std::string name =
        "network_" + p.name + "_" + std::to_string(static_cast<int>(km)) + "km.png";
    if (!terrain::writePng(o.directory / name, image)) {
        std::fprintf(stderr, "cannot write %s\n", name.c_str());
        return false;
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: %.0f km around (%.0f ; %.0f), %.1f s, cache %.1f MB (peak %.1f), %zu blocks\n",
                name.c_str(), km, p.x, p.z, seconds, static_cast<double>(net.cacheBytes()) * 1e-6,
                static_cast<double>(net.peakCacheBytes()) * 1e-6, net.blocksComputed());
    if (report != nullptr) {
        std::fprintf(report, "%s: %.0f km around (%.0f ; %.0f), %.1f s\n", name.c_str(), km, p.x,
                     p.z, seconds);
    }
    return true;
}

bool writeBasins(const Options& o, const terrain::DrainageNetwork& net, const Place& p) {
    terrain::Image basins{o.pixels, o.pixels, 3,
                          std::vector<std::uint8_t>(static_cast<std::size_t>(o.pixels) *
                                                    static_cast<std::size_t>(o.pixels) * 3)};
    const double spacing = 2048.0 * 1000.0 / o.pixels;
    network_tool::basinsMap(net.planetNetwork(), basins, p.x - 1024e3, p.z - 1024e3, spacing);
    return terrain::writePng(o.directory / ("basins_" + p.name + "_2048km.png"), basins);
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        std::fprintf(stderr, "usage: terrain_export_network <output-dir> --planet <file> "
                             "[--threads <n>] [--pixels <n>] [--place <name> <x> <z>]...\n");
        return 1;
    }
    std::filesystem::create_directories(o.directory);
    const auto start = std::chrono::steady_clock::now();
    const auto level = std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
        o.planet, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, terrain::PlanetParams{},
        terrain::WorldSize{}));
    const terrain::Heightfield relief(terrain::kDefaultWorldSeed);
    const terrain::DrainageNetwork net(terrain::kDefaultWorldSeed, relief.withoutCoasts(), level);
    std::printf("planet level and its network in %.1f s, the network %.1f MB\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
                static_cast<double>(net.planetNetwork().memoryBytes()) * 1e-6);
    std::FILE* report = std::fopen((o.directory / "network.txt").string().c_str(), "w");
    bool ok = true;
    for (const Place& p : o.places) {
        for (const double km : {2048.0, 200.0, 20.0}) {
            ok = ok && writeMap(o, relief, net, p, km, report);
        }
        ok = ok && writeBasins(o, net, p);
    }
    if (report != nullptr) {
        std::fclose(report);
    }
    return ok ? 0 : 1;
}
