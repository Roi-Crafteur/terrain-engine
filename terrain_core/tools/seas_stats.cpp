// The figures of the floors of the seas of a world beside those of the Earth (agent 51): the
// hypsometry of the ocean, its mean and median depths, the deepest point, the open floor far from
// the land, profiles from the shores out to sea (shelf break, continental slope, rise, trench) and
// the roughness of the deep floor. With --planet: the world with its water (and its seas).
//
// Usage: terrain_seas_stats [--planet <file>] [--seed <seed>] [--threads <n>] [--step <m>]
//                           [--transects <n>] [--list] [--find]
//   --step: the cells of the grid of the world (16384 by default; they must divide its sides)
//   --list: one line per profile
//   --find: places to look at (the deepest trench, a slow ridge, a fracture zone, the highest
//           seamount, an oceanic plateau), found on a grid of 32 km of the sea of the seas

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/io/sea_stats.hpp"
#include "terrain/storage/planet_file.hpp"
#include "terrain/storage/world_meta.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path planet;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::SeaStatsParams stats;
    bool list = false;
    bool find = false;
};

template <typename T> bool parse(std::string_view text, T& out) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size();
}

bool parseDouble(std::string_view text, double& out) {
    char* end = nullptr;
    const std::string copy(text);
    out = std::strtod(copy.c_str(), &end);
    return end != copy.c_str() && *end == '\0';
}

bool parseOptions(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        const bool more = i + 1 < argc;
        if (flag == "--planet" && more) {
            o.planet = argv[++i];
        } else if (flag == "--seed" && more) {
            if (!parse(argv[++i], o.seed)) {
                return false;
            }
        } else if (flag == "--threads" && more) {
            if (!parse(argv[++i], o.stats.threads) || o.stats.threads < 0) {
                return false;
            }
        } else if (flag == "--step" && more) {
            if (!parseDouble(argv[++i], o.stats.cellSize) || !(o.stats.cellSize > 0.0)) {
                return false;
            }
        } else if (flag == "--transects" && more) {
            if (!parse(argv[++i], o.stats.transects) || o.stats.transects < 0) {
                return false;
            }
        } else if (flag == "--find") {
            o.find = true;
        } else if (flag == "--list") {
            o.list = true;
        } else {
            return false;
        }
    }
    return true;
}

void printSpread(const char* what, const terrain::SeaSpread& s, double scale, const char* unit,
                 const char* earth) {
    if (s.count == 0) {
        std::printf("  %-34s none                                    | Earth: %s\n", what, earth);
        return;
    }
    std::printf("  %-34s %8.2f %s (p10 %8.2f, p90 %8.2f, %4zu) | Earth: %s\n", what,
                s.median * scale, unit, s.p10 * scale, s.p90 * scale, s.count, earth);
}

void printStats(const terrain::SeaStats& s, bool list) {
    std::printf("sea cells: %zu of %zu (%.1f %% of the world)\n", s.seaCells, s.cells,
                100.0 * static_cast<double>(s.seaCells) / static_cast<double>(s.cells));
    std::printf("hypsometry of the sea (share of its area)  world    Earth (Kossinna 1921)\n");
    float low = 0.0f;
    for (std::size_t b = 0; b < terrain::kSeaBandCount; ++b) {
        if (b < terrain::kSeaDepthBands.size()) {
            std::printf("  %5.0f - %5.0f m                          %6.1f %%  %6.1f %%\n", low,
                        terrain::kSeaDepthBands[b], 100.0 * s.bands[b],
                        100.0 * terrain::kEarthSeaBands[b]);
            low = terrain::kSeaDepthBands[b];
        } else {
            std::printf("  deeper than %5.0f m                     %6.1f %%  %6.1f %%\n", low,
                        100.0 * s.bands[b], 100.0 * terrain::kEarthSeaBands[b]);
        }
    }
    std::printf("mean depth %.0f m (Earth %.0f m), median %.0f m\n", s.meanDepth,
                terrain::kEarthMeanDepth, s.medianDepth);
    std::printf("deepest cell %.0f m at %.0f %.0f (Earth: 10,900 m, Challenger Deep)\n", s.deepest,
                s.deepestX, s.deepestZ);
    std::printf("open floor (>= 600 km from land): %zu cells, median %.0f m, 5 %% shallowest above "
                "%.0f m (Earth: abyssal plains 3,000 to 6,000 m, ridge crests about 2,500 m)\n",
                s.openCells, s.openMedianDepth, s.openShallowDepth);
    std::printf("profiles from the shore: %zu kept\n", s.transects.size());
    printSpread("shelf break depth", s.breakDepth, 1.0, "m ", "about 130 m (100 to 200)");
    printSpread("shelf break distance", s.breakDistance, 1.0e-3, "km", "about 80 km (0 to 1,500)");
    printSpread("continental slope", s.slopeDegrees, 1.0, "deg", "about 4 deg (3 to 6)");
    printSpread("continental rise (3 to 4 km)", s.riseDegrees, 1.0, "deg", "0.5 to 1 deg");
    printSpread("deepest within 400 km", s.trenchDepth, 1.0, "m ",
                "trenches 6,000 to 11,000 m on active margins");
    std::printf("deep floor roughness: %zu windows of 16 km, rms %.1f m (p90 %.1f m) | Earth: "
                "abyssal hills 50 to 300 m high, 1 to 10 km wide\n",
                s.hillWindows, s.hillRms, s.hillRmsP90);
    if (!list) {
        return;
    }
    std::printf("x z heading length breakDepth breakDistance slope rise deepest deepestDistance\n");
    for (const terrain::SeaTransect& t : s.transects) {
        std::printf("%.0f %.0f %.1f %.0f %s %s %s %s %.0f %.0f\n", t.x, t.z, t.heading, t.length,
                    t.shelfBreak ? std::to_string(t.breakDepth).c_str() : "-",
                    t.shelfBreak ? std::to_string(t.breakDistance).c_str() : "-",
                    t.slope ? std::to_string(t.slopeDegrees).c_str() : "-",
                    t.rise ? std::to_string(t.riseDegrees).c_str() : "-", t.deepest,
                    t.deepestDistance);
    }
}

// A best place of a kind and its score.
struct Found {
    double score = -1.0;
    double x = 0.0;
    double z = 0.0;
};

void keep(Found& f, double score, double x, double z) {
    if (score > f.score) {
        f = {score, x, z};
    }
}

// The places of the features of the seas on a grid of 32 km (Seas::mapAt).
void findPlaces(const terrain::Seas& seas, const terrain::WorldSize& world) {
    Found trench, ridge, fracture, seamount, plateau;
    constexpr double kStep = 32'768.0;
    for (double z = -0.5 * world.z + 0.5 * kStep; z < 0.5 * world.z; z += kStep) {
        for (double x = -0.5 * world.x + 0.5 * kStep; x < 0.5 * world.x; x += kStep) {
            const terrain::SeaPoint s = seas.mapAt(x, z, 0.0);
            if (!s.zone) {
                continue;
            }
            keep(trench, s.trench, x, z);
            if (s.ridge >= 0 && s.ridgeDistance < 10'000.0) {
                keep(ridge, 1.0 - s.rate, x, z);
            }
            keep(fracture, s.fracture, x, z);
            keep(seamount, s.seamount, x, z);
            keep(plateau, s.plateau > 0.99 ? -s.floor : -1.0, x, z);
        }
    }
    const auto print = [&](const char* what, const Found& f) {
        const terrain::SeaPoint s = seas.mapAt(f.x, f.z, 0.0);
        std::printf("  %-18s %.0f %.0f: floor %.0f m, trench %.0f m, ridge %d at %.0f m (rate "
                    "%.2f, age %.1f Myr), fracture %.0f m, seamount %.0f m, plateau %.2f\n",
                    what, f.x, f.z, s.floor, s.trench, s.ridge, s.ridgeDistance, s.rate, s.age,
                    s.fracture, s.seamount, s.plateau);
    };
    std::printf("places of the seas (grid of 32 km):\n");
    print("deepest trench", trench);
    print("slowest ridge axis", ridge);
    print("fracture zone", fracture);
    print("highest seamount", seamount);
    print("shallowest plateau", plateau);
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s [--planet <file>] [--seed <seed>] [--threads <n>] [--step <m>] "
                     "[--transects <n>] [--list] [--find]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    const auto start = std::chrono::steady_clock::now();
    const terrain::WorldSize world;
    const terrain::Heightfield relief(o.seed, terrain::HeightfieldParams{}, world);
    terrain::Heightfield field = relief;
    if (!o.planet.empty()) {
        terrain::PlanetParams params;
        params.threads = o.stats.threads;
        bool built = false;
        auto planet = std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
            o.planet, o.seed, terrain::HeightfieldParams{}, params, world, &built));
        std::printf("planet level %s %s (checksum %016llx)\n",
                    built ? "built and written to" : "read from", o.planet.string().c_str(),
                    static_cast<unsigned long long>(planet->checksum()));
        auto hydrology = std::make_shared<const terrain::Hydrology>(o.seed, relief, planet);
        field = relief.withHydrology(hydrology);
    } else {
        std::printf("no planet file: the relief and the sea of the geography only\n");
    }
    std::printf("seed %llu, generator %u, cells of %.0f m\n",
                static_cast<unsigned long long>(o.seed), terrain::kGeneratorVersion,
                o.stats.cellSize);
    const terrain::SeaStats stats = terrain::measureSeas(field, o.stats);
    printStats(stats, o.list);
    if (o.find && field.hydrology() != nullptr && field.hydrology()->seas() != nullptr) {
        findPlaces(*field.hydrology()->seas(), world);
    }
    std::printf("in %.1f s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return EXIT_SUCCESS;
}
