// The dev map of the world in a PNG (agent 35): the image terrain_core gives the dev map and the
// minimap of Unreal (map_render.hpp, map_source.hpp), to look at a place before going there.
// A box of the world (--box), or a box around a point at a spacing (--center and --mpp), or the
// whole world (--whole), on --size pixels; or --tiles n: the n x n tiles of the map pyramid around
// --center at --mpp (a power of two), each timed. Without --planet: the relief and the sea only.
// Prints where the samples came from, their counts, the queries of the hydrology and its courses.
// Usage: terrain_export_map [--planet <file>] [--seed <seed>] [--threads <n>]
//        [--layers <a,b,...>] [--max-samples <n>]
//        (--box <x> <z> <width> <height> | --center <x> <z> --mpp <m> | --whole)
//        (--size <width> <height> | --tiles <n>) <out.png>

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/io/png.hpp"
#include "terrain/map/map_render.hpp"
#include "terrain/map/map_source.hpp"
#include "terrain/map/map_style.hpp"
#include "terrain/storage/planet_file.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path planet;
    std::filesystem::path out;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    int threads = 0;
    std::vector<std::string> layers = terrain::defaultMapLayerNames();
    std::size_t maxSamples = terrain::kMapMaxSamples;
    std::optional<terrain::MapBox> box;
    std::optional<std::pair<double, double>> center;
    double mpp = 0.0;
    bool whole = false;
    int width = 0;
    int height = 0;
    int tiles = 0;
};

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

bool fail(const char* what, const char* text) {
    std::fprintf(stderr, "Invalid %s '%s'\n", what, text);
    return false;
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = std::min(text.find(',', start), text.size());
        out.push_back(text.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

bool parsePlace(const std::string& flag, int argc, char** argv, int& i, Options& o) {
    const auto need = [&](int count) { return i + count < argc; };
    if (flag == "--box" && need(4)) {
        terrain::MapBox b;
        i += 4;
        const bool ok = parse(argv[i - 3], b.x) && parse(argv[i - 2], b.z) &&
                        parse(argv[i - 1], b.width) && parse(argv[i], b.height) &&
                        b.width > 0.0 && b.height > 0.0;
        o.box = b;
        return ok || fail("box", argv[i - 3]);
    }
    if (flag == "--center" && need(2)) {
        std::pair<double, double> c;
        i += 2;
        o.center = c;
        return (parse(argv[i - 1], o.center->first) && parse(argv[i], o.center->second)) ||
               fail("center", argv[i - 1]);
    }
    if (flag == "--mpp" && need(1)) {
        return (parse(argv[++i], o.mpp) && o.mpp > 0.0) || fail("meters per pixel", argv[i]);
    }
    if (flag == "--whole") {
        o.whole = true;
        return true;
    }
    return fail("option", flag.c_str());
}

bool parseOption(int argc, char** argv, int& i, Options& o) {
    const std::string flag = argv[i];
    const auto need = [&](int count) { return i + count < argc; };
    if (flag == "--planet" && need(1)) {
        o.planet = argv[++i];
        return true;
    }
    if (flag == "--seed" && need(1)) {
        return parse(argv[++i], o.seed) || fail("seed", argv[i]);
    }
    if (flag == "--threads" && need(1)) {
        return (parse(argv[++i], o.threads) && o.threads >= 0) || fail("threads", argv[i]);
    }
    if (flag == "--layers" && need(1)) {
        o.layers = split(argv[++i]);
        return true;
    }
    if (flag == "--max-samples" && need(1)) {
        return (parse(argv[++i], o.maxSamples) && o.maxSamples >= 9) ||
               fail("max samples", argv[i]);
    }
    if (flag == "--size" && need(2)) {
        i += 2;
        return (parse(argv[i - 1], o.width) && parse(argv[i], o.height) && o.width >= 1 &&
                o.height >= 1 && o.width <= terrain::kMapMaxPixels &&
                o.height <= terrain::kMapMaxPixels) ||
               fail("size", argv[i - 1]);
    }
    if (flag == "--tiles" && need(1)) {
        return (parse(argv[++i], o.tiles) && o.tiles >= 1 && o.tiles <= 16) ||
               fail("tiles", argv[i]);
    }
    if (flag.size() > 1 && flag[0] == '-') {
        return parsePlace(flag, argc, argv, i, o);
    }
    if (!o.out.empty()) {
        return fail("argument", argv[i]);
    }
    o.out = argv[i];
    return true;
}

bool isPowerOfTwo(double v) {
    for (int level = 0; level <= terrain::kMapMaxLevel; ++level) {
        if (v == terrain::mapLevelMeters(level)) {
            return true;
        }
    }
    return false;
}

bool checkOptions(const Options& o) {
    const int places = (o.box ? 1 : 0) + (o.center ? 1 : 0) + (o.whole ? 1 : 0);
    if (places != 1) {
        return fail("place", "(one of --box, --center with --mpp, --whole)");
    }
    if (o.center.has_value() != (o.mpp > 0.0)) {
        return fail("place", "(--center goes with --mpp)");
    }
    if ((o.tiles > 0) == (o.width > 0)) {
        return fail("size", "(one of --size, --tiles)");
    }
    if (o.tiles > 0 && (!o.center || !isPowerOfTwo(o.mpp))) {
        return fail("tiles", "(--tiles needs --center and a power of two --mpp up to 32768)");
    }
    const terrain::MapLayers layers = terrain::defaultMapLayers();
    for (const std::string& name : o.layers) {
        if (layers.find(name) == nullptr) {
            return fail("layer", name.c_str());
        }
    }
    return !o.out.empty() || fail("output", "(none)");
}

bool parseOptions(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        if (!parseOption(argc, argv, i, o)) {
            return false;
        }
    }
    return checkOptions(o);
}

const char* kindName(terrain::MapSourceKind kind) {
    switch (kind) {
    case terrain::MapSourceKind::FineRelief:
        return "the fine relief with its water and the courses of the hydrology";
    case terrain::MapSourceKind::Relief:
        return "the relief alone with the water of the planet level";
    case terrain::MapSourceKind::PlanetCells:
        return "the cells of the planet level";
    case terrain::MapSourceKind::Synthetic:
        break;
    }
    return "synthetic";
}

// The image over a dark ground, as the dev map shows it.
terrain::Image toPng(const terrain::MapImage& map) {
    terrain::Image image{map.width, map.height, 3, {}};
    image.pixels.resize(static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height) * 3u);
    for (int j = 0; j < map.height; ++j) {
        for (int i = 0; i < map.width; ++i) {
            const terrain::MapColor c = terrain::over({25, 25, 30, 255}, map.at(i, j));
            const std::size_t at = 3u * (static_cast<std::size_t>(j) * static_cast<std::size_t>(map.width) +
                                         static_cast<std::size_t>(i));
            image.pixels[at] = c.r;
            image.pixels[at + 1] = c.g;
            image.pixels[at + 2] = c.b;
        }
    }
    return image;
}

void printStats(const terrain::MapImage& image, double seconds) {
    std::printf("  %d x %d pixels, stride %d: %zu samples (%zu of the relief, %zu of the planet "
                "level), %zu queries of the hydrology, %.3f s\n",
                image.width, image.height, image.stride, image.stats.samples,
                image.stats.reliefSamples, image.stats.planetSamples, image.stats.hydrologyCalls,
                seconds);
}

void printCourses(const terrain::Hydrology* hydrology) {
    if (hydrology != nullptr) {
        std::printf("hydrology: %zu courses computed, %.1f MB\n", hydrology->coursesComputed(),
                    static_cast<double>(hydrology->courseBytes()) / 1.0e6);
    }
}

terrain::MapFrame frameOf(const Options& o, const terrain::WorldSize& world) {
    if (o.whole) {
        return {{-0.5 * world.x, -0.5 * world.z, world.x, world.z}, o.width, o.height, world};
    }
    if (o.box) {
        return {*o.box, o.width, o.height, world};
    }
    const double w = o.mpp * o.width;
    const double h = o.mpp * o.height;
    return {{o.center->first - 0.5 * w, o.center->second - 0.5 * h, w, h}, o.width, o.height,
            world};
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// The n x n tiles around the center, each timed, put together.
terrain::MapImage renderTiles(const Options& o, const terrain::MapWorldSource& source,
                              const terrain::MapLayers& layers, const terrain::WorldSize& world) {
    const int level = terrain::mapLevelFor(o.mpp);
    const terrain::MapTileKey middle =
        terrain::mapTileAt(level, o.center->first, o.center->second, world);
    const int side = o.tiles * terrain::kMapTilePixels;
    terrain::MapImage all;
    all.width = side;
    all.height = side;
    all.rgba.assign(4u * static_cast<std::size_t>(side) * static_cast<std::size_t>(side), 0);
    for (int tz = 0; tz < o.tiles; ++tz) {
        for (int tx = 0; tx < o.tiles; ++tx) {
            const terrain::MapTileKey key{level, middle.tx + tx - o.tiles / 2,
                                          middle.tz + tz - o.tiles / 2};
            const auto start = std::chrono::steady_clock::now();
            const terrain::MapImage tile = terrain::renderMap(
                source, terrain::mapTileFrame(key, world), layers, o.layers,
                {o.threads, o.maxSamples});
            std::printf("tile (%d, %lld, %lld):", level, static_cast<long long>(key.tx),
                        static_cast<long long>(key.tz));
            printStats(tile, secondsSince(start));
            for (int j = 0; j < terrain::kMapTilePixels; ++j) {
                const std::size_t row = static_cast<std::size_t>(tz * terrain::kMapTilePixels + j);
                std::copy_n(tile.rgba.begin() + 4 * j * terrain::kMapTilePixels,
                            4 * terrain::kMapTilePixels,
                            all.rgba.begin() + static_cast<std::ptrdiff_t>(
                                                   4u * (row * static_cast<std::size_t>(side) +
                                                         static_cast<std::size_t>(tx) *
                                                             terrain::kMapTilePixels)));
            }
        }
    }
    return all;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s [--planet <file>] [--seed <seed>] [--threads <n>] "
                     "[--layers <a,b,...>] [--max-samples <n>] (--box <x> <z> <width> <height> | "
                     "--center <x> <z> --mpp <m> | --whole) (--size <width> <height> | "
                     "--tiles <n>) <out.png>\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    const terrain::WorldSize world;
    const terrain::Heightfield relief(o.seed, terrain::HeightfieldParams{}, world);
    std::shared_ptr<const terrain::Hydrology> hydrology;
    if (!o.planet.empty()) {
        terrain::PlanetParams params;
        params.threads = o.threads;
        bool built = false;
        auto planet = std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
            o.planet, o.seed, terrain::HeightfieldParams{}, params, world, &built));
        std::printf("planet level %s %s\n", built ? "built and written to" : "read from",
                    o.planet.string().c_str());
        hydrology = std::make_shared<const terrain::Hydrology>(o.seed, relief, planet);
    } else {
        std::printf("no planet file: the relief and the sea only, no rivers nor lakes\n");
    }
    const terrain::TerrainTiles tiles(o.seed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt, world, hydrology);
    const terrain::MapWorldSource source(tiles);
    const terrain::MapLayers layers = terrain::defaultMapLayers();
    terrain::MapImage image;
    if (o.tiles > 0) {
        std::printf("source: %s\n", kindName(source.kindFor(o.mpp)));
        image = renderTiles(o, source, layers, world);
    } else {
        const terrain::MapFrame frame = frameOf(o, world);
        std::printf("source: %s\n", kindName(source.kindFor(std::max(frame.stepX(), frame.stepZ()))));
        const auto start = std::chrono::steady_clock::now();
        image = terrain::renderMap(source, frame, layers, o.layers, {o.threads, o.maxSamples});
        printStats(image, secondsSince(start));
    }
    printCourses(hydrology.get());
    if (!terrain::writePng(o.out, toPng(image))) {
        std::fprintf(stderr, "Cannot write %s\n", o.out.string().c_str());
        return EXIT_FAILURE;
    }
    std::printf("wrote %s\n", o.out.string().c_str());
    return EXIT_SUCCESS;
}
