// Computes the planet level of a world, prints what it measured (time per stage, peak memory
// during the construction and memory kept afterwards, figures of the land, the basins and the
// rivers, false passes) and writes its control images. Without the optional arguments: seed
// 20261019, cells of 8,192 m, the planetary preset, one relief sample per cell, all the
// hardware threads, the default world (60,030,976 x 34,996,224 m). Since generator 3 the default
// and planetary reliefs are both the planetary geography of phase 11R; the legacy relief is the
// planetary preset of generator 2 (plates 0.5, macro continent 0.25, geography off), for
// comparisons with earlier figures; geography is an older name of planetary.
// Usage: terrain_export_planet <output-dir> [seed] [cell-size] [default|planetary|legacy|geography]
//        [samples-per-axis] [threads] [world-x] [world-z]

#include "terrain/generation/planet/planet_false_passes.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/planet/planet_stats.hpp"
#include "terrain/io/planet_png.hpp"
#include "terrain/io/relief_lakes.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

struct Options {
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    double cellSize = 8192.0;
    const char* relief = "planetary";
    int samples = 1;
    int threads = 0;
    terrain::WorldSize world;
};

terrain::HeightfieldParams reliefParams(const char* relief) {
    if (std::strcmp(relief, "default") == 0) {
        return {};
    }
    terrain::HeightfieldParams params = terrain::planetaryHeightfieldParams();
    if (std::strcmp(relief, "legacy") == 0) {
        params.geography.enabled = false;
    }
    return params;
}

bool fail(const char* what, const char* text, const char* expected) {
    std::fprintf(stderr, "Invalid %s '%s': expected %s\n", what, text, expected);
    return false;
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
    try {
        (void)terrain::makePlanetGrid(options.world, options.cellSize);
    } catch (const std::invalid_argument&) {
        return fail("cell size", argc > 3 ? argv[3] : "8192",
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
    if (argc > 3 && !(parse(argv[3], options.cellSize) && std::isfinite(options.cellSize) &&
                      options.cellSize > 0.0)) {
        return fail("cell size", argv[3], "a number of meters above 0");
    }
    if (argc > 4) {
        bool known = false;
        for (const char* relief : {"default", "planetary", "legacy", "geography"}) {
            if (std::strcmp(argv[4], relief) == 0) {
                options.relief = relief;
                known = true;
            }
        }
        if (!known) {
            return fail("relief", argv[4], "default, planetary, legacy or geography");
        }
    }
    if (argc > 5 &&
        !(parse(argv[5], options.samples) && options.samples >= 1 && options.samples <= 8)) {
        return fail("samples per axis", argv[5], "an integer from 1 to 8");
    }
    if (argc > 6 && !(parse(argv[6], options.threads) && options.threads >= 0)) {
        return fail("threads", argv[6], "an integer of at least 0 (0: all the hardware threads)");
    }
    return parseWorld(argc, argv, options);
}

// Peak resident memory of the process so far, in bytes (0 if unknown).
std::size_t peakMemory() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return counters.PeakWorkingSetSize;
    }
    return 0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<std::size_t>(usage.ru_maxrss); // bytes
#else
    return static_cast<std::size_t>(usage.ru_maxrss) * 1024; // kilobytes
#endif
#endif
}

double megabytes(std::size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

const char* mouthName(terrain::RiverMouth mouth) {
    constexpr std::array<const char*, 6> names = {"confluence",    "sea",          "lake",
                                                  "terminal lake", "closed basin", "ice"};
    return names[static_cast<std::size_t>(mouth)];
}

void printTimes(const terrain::PlanetTimings& t, double stats, double passes, double images) {
    std::printf("time: relief %.1f s, ocean %.1f s, sills %.1f s, climate %.1f s, order %.1f s, "
                "hierarchy %.1f s, lakes %.1f s, discharge %.1f s, rivers %.1f s, features %.1f s, "
                "crossings %.1f s; level %.1f s; figures %.1f s, false passes %.1f s, "
                "images %.1f s\n",
                t.relief, t.ocean, t.breach, t.climate, t.order, t.hierarchy, t.lakes, t.flow,
                t.rivers, t.features, t.passes, t.total(), stats, passes, images);
}

void printBreach(const terrain::PlanetBreach& b) {
    std::printf("sills: %zu closed depressions, %zu breached (%.0f km2), %zu kept (%.0f km2); %zu "
                "cells lowered, by %.1f m at most\n",
                b.depressions, b.breached, b.breachedArea / 1.0e6, b.depressions - b.breached,
                b.keptArea / 1.0e6, b.cutCells, static_cast<double>(b.deepestCut));
}

void printPasses(const terrain::PlanetPasses& p) {
    std::printf("crossings: %zu links measured, %zu cells raised into the sills of lakes (by %.0f m "
                "at most), the rivers found again %zu times\n",
                p.checked, p.raised, static_cast<double>(p.highestRaise), p.rounds);
}

void printLand(const terrain::PlanetStats& stats) {
    double land = 0.0;
    for (const double area : stats.landMasses) {
        land += area;
    }
    std::size_t continents = 0;
    for (const double area : stats.landMasses) {
        continents += area >= 1.0e12 ? 1u : 0u;
    }
    std::printf("land: %.1f%% of the planet, %zu land masses, %zu of at least 1,000,000 km2; "
                "largest (km2):",
                100.0 * stats.landShare, stats.landMasses.size(), continents);
    for (std::size_t k = 0; k < stats.landMasses.size() && k < 8; ++k) {
        std::printf(" %.0f", stats.landMasses[k] / 1.0e6);
    }
    std::printf("\nlakes: %zu (%zu overflowing, %zu terminal, %zu playas), %.0f km2, %.1f%% of "
                "the land; rivers: %.0f km\n",
                stats.lakes, stats.overflowingLakes, stats.terminalLakes, stats.playas,
                stats.lakeArea / 1.0e6, 100.0 * stats.lakeArea / land, stats.riverLength / 1000.0);
}

void printBasinsAndRivers(const terrain::PlanetStats& stats) {
    for (std::size_t k = 0; k < stats.largestBasins.size(); ++k) {
        const terrain::PlanetBasinStat& b = stats.largestBasins[k];
        std::printf("basin %zu: %.0f km2, %s at (%.0f, %.0f) km, %.0f m3/s, %.0f%% frozen\n", k + 1,
                    b.area / 1.0e6, b.sea ? "sea" : "terminal lake", b.x / 1000.0, b.z / 1000.0,
                    b.discharge, 100.0 * b.frozenShare);
    }
    for (std::size_t k = 0; k < stats.longestRivers.size(); ++k) {
        const terrain::PlanetRiverStat& r = stats.longestRivers[k];
        std::printf(
            "river %zu: %.0f km (%.0f across lakes), %.0f m3/s, into %s at (%.0f, %.0f) km, "
            "Strahler %d\n",
            k + 1, r.length / 1000.0, r.lakeLength / 1000.0, r.discharge, mouthName(r.mouth),
            r.x / 1000.0, r.z / 1000.0, static_cast<int>(r.strahler));
    }
}

void printFeaturesAndPasses(const terrain::PlanetLevel& level, const terrain::FalsePassStats& p) {
    std::array<std::size_t, 4> kinds{};
    for (const terrain::RiverFeature& f : level.features()) {
        ++kinds[static_cast<std::size_t>(f.kind)];
    }
    std::printf("features: %zu deltas, %zu estuaries, %zu waterfalls, %zu gorges\n", kinds[0],
                kinds[1], kinds[2], kinds[3]);
    const double km = p.sampled / 1000.0;
    const auto share = [&](double length) { return km > 0.0 ? 100.0 * length / p.sampled : 0.0; };
    std::printf("false passes: %.0f km of river sampled; fine relief above the river by more than "
                "50 m: %.0f km (%.1f%%), 200 m: %.0f km (%.1f%%), 500 m: %.0f km (%.1f%%); "
                "highest %.0f m at (%.0f, %.0f) km\n",
                km, p.over50 / 1000.0, share(p.over50), p.over200 / 1000.0, share(p.over200),
                p.over500 / 1000.0, share(p.over500), static_cast<double>(p.maxExcess),
                p.maxX / 1000.0, p.maxZ / 1000.0);
    std::printf("false passes over 200 m by Strahler order:");
    for (std::size_t order = 1; order < p.sampledByOrder.size(); ++order) {
        if (p.sampledByOrder[order] > 0.0) {
            std::printf(" %zu: %.1f%% of %.0f km", order,
                        100.0 * p.over200ByOrder[order] / p.sampledByOrder[order],
                        p.sampledByOrder[order] / 1000.0);
        }
    }
    std::printf("\n");
}

double seconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArguments(argc, argv, options)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [seed] [cell-size] "
                     "[default|planetary|legacy|geography] "
                     "[samples-per-axis] [threads] [world-x] [world-z]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    terrain::PlanetParams params;
    params.cellSize = options.cellSize;
    params.samplesPerAxis = options.samples;
    params.threads = options.threads;
    const terrain::Heightfield field(options.seed, reliefParams(options.relief), options.world);
    const terrain::PlanetLevel level(terrain::planet::buildPlanet(field, params), params);
    const std::size_t peak = peakMemory();
    auto start = std::chrono::steady_clock::now();
    const terrain::PlanetStats stats = terrain::planet::planetStats(level, 10);
    const double statsTime = seconds(start);
    start = std::chrono::steady_clock::now();
    const terrain::FalsePassStats passes = terrain::planet::measureFalsePasses(
        level.rivers(), [&field](double x, double z) { return field.heightAt(x, z); },
        options.threads);
    const double passesTime = seconds(start);
    start = std::chrono::steady_clock::now();
    if (!terrain::exportPlanetPngs(level, argv[1])) {
        std::fprintf(stderr, "Failed to write the planet images into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const double imagesTime = seconds(start);
    const terrain::PlanetGrid& grid = level.grid();
    const int threads = options.threads > 0
                            ? options.threads
                            : std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    std::printf("seed %llu, %s relief, %d x %d cells of %.0f m, %d x %d samples, %d threads: "
                "checksum %016llx, %s/planet_*.png\n",
                static_cast<unsigned long long>(options.seed),
                options.relief, grid.nx, grid.nz, grid.cellSize,
                options.samples, options.samples, threads,
                static_cast<unsigned long long>(level.checksum()), argv[1]);
    printTimes(level.timings(), statsTime, passesTime, imagesTime);
    printBreach(level.breach());
    printPasses(level.passes());
    std::printf("memory: peak %.0f MB during the construction, %.0f MB kept by the level\n",
                megabytes(peak), megabytes(level.memoryBytes()));
    printLand(stats);
    std::printf("lakes at least 500 m deep (level above their lowest cell): %.0f%% of the lake "
                "area\n",
                100.0 * terrain::deepLakeShare(level));
    printBasinsAndRivers(stats);
    printFeaturesAndPasses(level, passes);
    return EXIT_SUCCESS;
}
