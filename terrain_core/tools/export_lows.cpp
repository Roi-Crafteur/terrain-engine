// The land below the sea level of a world (agent 52): measures and a map. Writes to the output
// directory lows.txt (printed too) and lows_map.png:
// - the land (every sample the field does not put under the sea, whatever its height: a dry
//   depression below the sea level is land) on cells of --cell meters over the whole world: its
//   share below 0 m and below +50 m, its median height, the median depth of the land below 0 m,
//   the water over it (dry, salt lake, lake, river), its largest dry stretches below 0 m;
// - the closed lows (today the depressions of phase 11B): the water at the lowest point of each
//   (dry, salt lake, lake) by the climate belt of the planet level at its center;
// - what the readers make of the dry land below 0 m: its biome, the surface blend and the plants
//   of the tiles over it; the low land far from the sea taken for a beach or a marsh (agent 49);
//   the river points held at the sea level over a dry low (agent 31);
// - with --list, every low (family, center, reach, lowest point, water, belt).
// With --planet, the world has its planet level (read from the file, or built and written there):
// its rivers, lakes and climate. The figures are the same whatever the threads.
// Usage: terrain_export_lows <output-dir> [--planet <file>] [--seed <seed>] [--world <x> <z>]
//        [--threads <n>] [--cell <meters>] [--width <pixels>] [--readers <n>] [--list]

#include "lows_readers.hpp"
#include "lows_sites.hpp"
#include "lows_world.hpp"

#include "terrain/generation/water/hydrology.hpp"
#include "terrain/storage/planet_file.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

using lows_tool::Point;

// The coast distance over which the continentalness of the Heightfield reaches 1.
constexpr double kContinentalScale = 1'500'000.0;
// The low land far from the sea (the beaches and marshes of agent 49).
constexpr float kLowTop = 15.0f;
constexpr double kLowInland = 40'000.0;
constexpr std::size_t kStretches = 20;

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

struct Options {
    std::filesystem::path directory;
    std::filesystem::path planet;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::WorldSize world;
    int threads = 0;
    double cell = 8192.0;
    int width = 1832;
    std::size_t readers = 400;
    bool list = false;
};

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
        std::ofstream out(path);
        out << text_;
        return static_cast<bool>(out);
    }

private:
    std::string text_;
};

bool fail(const char* what, const char* text) {
    std::fprintf(stderr, "Invalid %s '%s'\n", what, text);
    return false;
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
    if (flag == "--world" && need(2)) {
        i += 2;
        return (parse(argv[i - 1], o.world.x) && parse(argv[i], o.world.z)) ||
               fail("world", argv[i - 1]);
    }
    if (flag == "--threads" && need(1)) {
        return (parse(argv[++i], o.threads) && o.threads >= 0) || fail("threads", argv[i]);
    }
    if (flag == "--cell" && need(1)) {
        return (parse(argv[++i], o.cell) && o.cell > 0.0) || fail("cell", argv[i]);
    }
    if (flag == "--width" && need(1)) {
        return (parse(argv[++i], o.width) && o.width >= 1) || fail("width", argv[i]);
    }
    if (flag == "--readers" && need(1)) {
        return parse(argv[++i], o.readers) || fail("readers", argv[i]);
    }
    if (flag == "--list") {
        o.list = true;
        return true;
    }
    return fail("option", argv[i]);
}

bool parseOptions(int argc, char** argv, Options& o) {
    if (argc < 2 || argv[1][0] == '-') {
        return false;
    }
    o.directory = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (!parseOption(argc, argv, i, o)) {
            return false;
        }
    }
    try {
        terrain::validate(o.world);
    } catch (const std::invalid_argument&) {
        return fail("world size", "(multiples of 65,536 m expected)");
    }
    if (std::fmod(o.world.x, o.cell) != 0.0 || std::fmod(o.world.z, o.cell) != 0.0) {
        return fail("cell", "(it must divide both sides of the world)");
    }
    return true;
}

double share(std::size_t part, std::size_t whole) {
    return whole > 0 ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
}

void reportWorld(const lows_tool::WorldFigures& w, double cell, Report& r) {
    r.add("land (not the sea) on cells of %.0f m: %zu samples of %zu\n", cell, w.land, w.samples);
    r.add("  below 0 m: %.3f %% of the land; below +50 m: %.2f %%\n", share(w.below0, w.land),
          share(w.below50, w.land));
    r.add("  median height of the land %.1f m; median depth of the land below 0 m %.1f m\n",
          static_cast<double>(w.medianLand), static_cast<double>(w.medianDepth));
    r.add("  the land below 0 m: dry %zu, salt lake %zu, lake %zu, river %zu samples\n",
          w.dryBelow, w.saltBelow, w.lakeBelow, w.riverBelow);
    r.add("largest dry stretches below 0 m:\n");
    for (const lows_tool::Stretch& s : w.largest) {
        r.add("  %8.0f km2, lowest %7.1f m at (%10.0f, %10.0f)\n", s.area / 1.0e6,
              static_cast<double>(s.lowest), s.lowX, s.lowZ);
    }
}

void reportLows(const std::vector<lows_tool::LowFigures>& lows, bool list, Report& r) {
    r.add("closed lows: %zu\n", lows.size());
    for (const lows_tool::BeltTable& t : lows_tool::beltTables(lows)) {
        r.add("  %s by belt (dry / salt lake / lake / other):\n", t.family.c_str());
        for (std::size_t b = 0; b < lows_tool::kBelts; ++b) {
            const auto& c = t.counts[b];
            if (c[0] + c[1] + c[2] + c[3] > 0) {
                r.add("    %-10s %4zu %4zu %4zu %4zu\n",
                      lows_tool::beltName(static_cast<lows_tool::Belt>(b)), c[0], c[1], c[2],
                      c[3]);
            }
        }
    }
    for (std::size_t k = 0; list && k < lows.size(); ++k) {
        const lows_tool::LowFigures& f = lows[k];
        r.add("  #%zu %s at (%.0f, %.0f), reach %.0f m: lowest %.1f m at (%.0f, %.0f), %s, %s\n",
              k, f.low.family.c_str(), f.low.x, f.low.z, f.low.reach,
              static_cast<double>(f.lowest), f.lowX, f.lowZ, lows_tool::lowWaterName(f.water),
              lows_tool::beltName(f.belt));
    }
}

void reportReaders(const lows_tool::ReaderFigures& f, Report& r) {
    r.add("readers of %zu dry points below 0 m: biome ocean %zu, beach %zu, marsh %zu\n",
          f.points, f.biomeOcean, f.biomeBeach, f.biomeMarsh);
    r.add("  tiles of LOD 0 over them: %zu, surface blend ocean first %zu, water first %zu; "
          "plants %zu, of them below 0 m %zu\n",
          f.tiles, f.blendOcean, f.blendWater, f.plantsTotal, f.plantsBelow);
    r.add("  for agent 49: of %zu dry points at 0 to %.0f m over %.0f km from the sea, beach %zu, "
          "marsh %zu\n",
          f.lowPoints, static_cast<double>(kLowTop), kLowInland / 1000.0, f.lowBeach, f.lowMarsh);
    r.add("  for agent 31 (water/hydrology.cpp:248): of %zu river points near the dry lows, %zu "
          "held at the sea level over ground below -1 m, over %.0f km from the sea\n",
          f.riverPoints, f.riverHeld, 20.0);
    for (const Point& p : f.heldExamples) {
        r.add("    held at (%.0f, %.0f)\n", p.x, p.z);
    }
}

// The dry points below 0 m and the low points far from the sea of the grid, row by row.
void pointsOf(const terrain::ReliefGrid& grid, const terrain::Heightfield& field,
              std::vector<Point>& dry, std::vector<Point>& low) {
    for (int j = 0; j < grid.rows; ++j) {
        for (int i = 0; i < grid.columns; ++i) {
            const std::size_t cell = grid.index(i, j);
            if (grid.seaAt(cell) || grid.water[cell] != terrain::WaterKind::None) {
                continue;
            }
            const float h = grid.heights[cell];
            if (h < terrain::kSeaLevel) {
                dry.push_back({grid.x(i), grid.z(j)});
            } else if (h <= kLowTop) {
                const double inland =
                    static_cast<double>(field.sampleAt(grid.x(i), grid.z(j)).continentalness) *
                    kContinentalScale;
                if (inland > kLowInland) {
                    low.push_back({grid.x(i), grid.z(j)});
                }
            }
        }
    }
}

std::shared_ptr<const terrain::PlanetLevel> planetOf(const Options& o, Report& r) {
    if (o.planet.empty()) {
        r.add("no planet level: the relief alone, no rivers, lakes nor climate\n");
        return nullptr;
    }
    terrain::PlanetParams params;
    params.threads = o.threads;
    bool built = false;
    auto level = std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
        o.planet, o.seed, terrain::HeightfieldParams{}, params, o.world, &built));
    r.add("planet level %s %s, checksum %016llx\n", built ? "built into" : "read from",
          o.planet.string().c_str(), static_cast<unsigned long long>(level->checksum()));
    return level;
}

int run(const Options& o) {
    std::error_code error;
    std::filesystem::create_directories(o.directory, error);
    Report r;
    r.add("lows of seed %llu, world %.0f x %.0f km\n", static_cast<unsigned long long>(o.seed),
          o.world.x / 1000.0, o.world.z / 1000.0);
    const terrain::Heightfield relief(o.seed, terrain::HeightfieldParams{}, o.world);
    const std::shared_ptr<const terrain::PlanetLevel> planet = planetOf(o, r);
    std::shared_ptr<const terrain::Hydrology> hydrology;
    if (planet) {
        hydrology = std::make_shared<const terrain::Hydrology>(o.seed, relief, planet);
    }
    const terrain::Heightfield field = hydrology ? relief.withHydrology(hydrology) : relief;
    terrain::ReliefGrid grid = terrain::sampleReliefWorld(relief, o.cell, o.threads);
    std::size_t riverCells = 0;
    if (planet) {
        lows_tool::addPlanetWater(grid, *planet, riverCells);
    }
    const lows_tool::WorldFigures world = lows_tool::measureWorld(grid, kStretches);
    reportWorld(world, o.cell, r);
    r.add("  (the relief before the valleys of the rivers, the water of the planet level; %zu of "
          "the samples below 0 m lie in a river cell of the planet)\n",
          riverCells);
    std::vector<lows_tool::LowFigures> lows;
    std::vector<Point> centers;
    for (const lows_tool::Low& low : lows_tool::lowsOf(relief)) {
        lows.push_back(lows_tool::measureLow(field, planet.get(), low));
        centers.push_back({low.x, low.z});
    }
    reportLows(lows, o.list, r);
    const terrain::TerrainTiles tiles(o.seed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt, o.world, hydrology);
    std::vector<Point> dry;
    std::vector<Point> low;
    pointsOf(grid, relief, dry, low);
    reportReaders(lows_tool::measureReaders(field, tiles, dry, low, centers, o.readers), r);
    const int width = o.width > grid.columns ? grid.columns : o.width;
    const bool written =
        grid.columns % width == 0 &&
        terrain::writePng(o.directory / "lows_map.png", lows_tool::lowsMap(grid, width));
    if (!written || !r.write(o.directory / "lows.txt")) {
        std::fprintf(stderr, "Failed to write into %s (the width must divide %d)\n",
                     o.directory.string().c_str(), grid.columns);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // each line as soon as it is known
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [--planet <file>] [--seed <seed>] [--world <x> <z>] "
                     "[--threads <n>] [--cell <meters>] [--width <pixels>] [--readers <n>] "
                     "[--list]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    try {
        return run(o);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "terrain_export_lows: %s\n", e.what());
        return EXIT_FAILURE;
    }
}
