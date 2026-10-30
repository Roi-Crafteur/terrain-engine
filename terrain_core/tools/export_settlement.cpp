// The settlement V1 of a world (docs/PEUPLEMENT.md): its analysis maps on the planet level,
// written as PNG control images (settlement_*.png, resource_*.png), with a report
// (settlement.txt, also printed): the seconds of each stage, the memory, the counts of the
// geology and of the magic, the statistics of each map and of the raceSuitability of each people
// (suitability_<people>.png for the humans, the dwarves and the elves), the fidelity of the
// roughness, and with
// --equity-seeds the share of the telluric lines and nodes of several networks on each terrain
// model against its share of the land.
// The planet level is built in memory, or read from --planet <file>, which is never written in
// silence: a missing, damaged or stale file stops the tool with the reason; --rebuild-planet
// builds it again into <file>.new, reads it back, then replaces <file> with it.
// Usage: terrain_export_settlement <output-dir> [--planet <file>] [--rebuild-planet]
//        [--seed <seed>] [--world <x> <z>] [--cell <meters>] [--threads <n>] [--data <dir>]
//        [--width <pixels>] [--equity-seeds <n>] [--preset <id>|all]
//        [--zoom <x> <z> <width-km> <height-km>]

#include "process_memory.hpp"
#include "settlement_kingdoms.hpp"
#include "settlement_report.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/settlement/borders.hpp"
#include "terrain/generation/settlement/expansion.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/generation/settlement/suitability.hpp"
#include "terrain/generation/settlement/world_preset.hpp"
#include "terrain/io/planet_png.hpp"
#include "terrain/io/png.hpp"
#include "terrain/io/settlement_png.hpp"
#include "terrain/storage/planet_file.hpp"
#include "terrain/storage/storage_error.hpp"
#include "terrain/storage/world_meta.hpp"

#include <charconv>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace st = terrain::settlement;
using settlement_tool::mebibytes;
using settlement_tool::Report;

namespace {

// A finite number (std::strtod: the floating from_chars is missing on some compilers).
bool parseReal(const char* text, double& value) {
    char* end = nullptr;
    value = std::strtod(text, &end);
    return end != text && *end == '\0' && std::isfinite(value);
}

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

struct Options {
    std::filesystem::path directory;
    std::filesystem::path planet;
    bool rebuildPlanet = false;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::WorldSize world;
    std::uint64_t cell = 8192;
    int threads = 0;
    std::filesystem::path data = TERRAIN_SETTLEMENT_DATA;
    int width = 2048;
    int equitySeeds = 0;
    std::string preset; // empty: the default of the data; "all": every preset
    bool zoom = false;
    double zoomX = 0.0;
    double zoomZ = 0.0;
    double zoomWidthKm = 0.0;
    double zoomHeightKm = 0.0;
};

bool parseFlag(int argc, char** argv, int& i, Options& o) {
    const std::string flag = argv[i];
    const bool one = i + 1 < argc;
    if (flag == "--planet" && one) {
        o.planet = argv[++i];
        return true;
    }
    if (flag == "--rebuild-planet") {
        o.rebuildPlanet = true;
        return true;
    }
    if (flag == "--preset" && one) {
        o.preset = argv[++i];
        return true;
    }
    if (flag == "--data" && one) {
        o.data = argv[++i];
        return true;
    }
    if (flag == "--seed" && one) {
        return parse(argv[++i], o.seed);
    }
    if (flag == "--cell" && one) {
        return parse(argv[++i], o.cell) && o.cell > 0;
    }
    if (flag == "--threads" && one) {
        return parse(argv[++i], o.threads) && o.threads >= 0;
    }
    if (flag == "--width" && one) {
        return parse(argv[++i], o.width) && o.width > 0;
    }
    if (flag == "--equity-seeds" && one) {
        return parse(argv[++i], o.equitySeeds) && o.equitySeeds >= 0 && o.equitySeeds <= 64;
    }
    if (flag == "--zoom" && i + 4 < argc) {
        o.zoom = parseReal(argv[i + 1], o.zoomX) && parseReal(argv[i + 2], o.zoomZ) &&
                 parseReal(argv[i + 3], o.zoomWidthKm) && parseReal(argv[i + 4], o.zoomHeightKm) &&
                 o.zoomWidthKm > 0.0 && o.zoomHeightKm > 0.0;
        i += 4;
        return o.zoom;
    }
    if (flag == "--world" && i + 2 < argc) {
        const bool ok = parse(argv[i + 1], o.world.x) && parse(argv[i + 2], o.world.z);
        i += 2;
        return ok;
    }
    return false;
}

bool parseOptions(int argc, char** argv, Options& o) {
    if (argc < 2 || argv[1][0] == '-') {
        std::fprintf(stderr, "Expected an output directory\n");
        return false;
    }
    o.directory = argv[1];
    for (int i = 2; i < argc; ++i) {
        const int at = i;
        if (!parseFlag(argc, argv, i, o)) {
            std::fprintf(stderr, "Invalid option '%s'\n", argv[at]);
            return false;
        }
    }
    if (o.rebuildPlanet && o.planet.empty()) {
        std::fprintf(stderr, "Invalid option '--rebuild-planet' without --planet <file>\n");
        return false;
    }
    return true;
}

double since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// What a planet file must hold for these options.
std::string expected(const Options& o) {
    char text[256];
    std::snprintf(text, sizeof text,
                  "expected: seed %llu, generator version %u, planet file version %u, world "
                  "%.0f x %.0f m, cells of %llu m",
                  static_cast<unsigned long long>(o.seed),
                  static_cast<unsigned>(terrain::kGeneratorVersion),
                  static_cast<unsigned>(terrain::kPlanetFileVersion), o.world.x, o.world.z,
                  static_cast<unsigned long long>(o.cell));
    return text;
}

// Builds the level again into <file>.new, reads it back, then replaces <file> with it; <file>
// stays as it was if anything fails.
terrain::PlanetLevel rebuildPlanetFile(const Options& o, const terrain::PlanetParams& params) {
    terrain::PlanetLevel level(o.seed, terrain::HeightfieldParams{}, params, o.world);
    std::filesystem::path fresh = o.planet;
    fresh += ".new";
    std::error_code error;
    std::filesystem::remove(fresh, error);
    try {
        terrain::savePlanetFile(fresh, level, o.seed, o.world);
        if (terrain::loadPlanetFile(fresh, o.seed, params, o.world).checksum() !=
            level.checksum()) {
            throw std::runtime_error("the file written does not read back the same level");
        }
        std::filesystem::rename(fresh, o.planet);
    } catch (const std::exception& e) {
        std::filesystem::remove(fresh, error);
        throw std::runtime_error("Invalid planet file " + o.planet.string() +
                                 ": cannot be rebuilt (" + e.what() + "); it was left as it was");
    }
    return level;
}

terrain::PlanetLevel openLevel(const Options& o, const terrain::PlanetParams& params) {
    if (o.planet.empty()) {
        return terrain::PlanetLevel(o.seed, terrain::HeightfieldParams{}, params, o.world);
    }
    if (o.rebuildPlanet) {
        return rebuildPlanetFile(o, params);
    }
    std::error_code error;
    if (!std::filesystem::exists(o.planet, error)) {
        throw std::runtime_error("Invalid planet file " + o.planet.string() + ": missing (" +
                                 expected(o) + "); use --rebuild-planet to build it");
    }
    try {
        return terrain::loadPlanetFile(o.planet, o.seed, params, o.world);
    } catch (const terrain::StorageError& e) {
        throw std::runtime_error(std::string("Invalid planet file: ") + e.what() + " (" +
                                 expected(o) +
                                 "); left untouched, use --rebuild-planet to build it again");
    }
}

// The images of the analysis maps, and suitability_<people>.png for the peoples of section 4.
bool writeImages(const Options& o, const terrain::PlanetLevel& level,
                 const st::SettlementMaps& maps,
                 const st::SettlementData& data, const st::SuitabilityMaps& s) {
    if (!terrain::exportSettlementMapPngs(level, maps, data, o.directory, o.width)) {
        return false;
    }
    const int scale = terrain::planetImageScale(level.grid(), o.width);
    for (const char* id : {"humains", "nains", "elfes"}) {
        const int k = data.people(id);
        if (k >= 0 && !terrain::writePng(o.directory / (std::string("suitability_") + id + ".png"),
                                         terrain::renderSettlementMap(
                                             level, maps, s.byPeople[static_cast<std::size_t>(k)],
                                             terrain::SettlementPalette::Habitability, scale))) {
            return false;
        }
    }
    return true;
}

// ridges_zoom.png and compartments_zoom.png of the window.
bool writeZoom(const Options& o, const terrain::PlanetLevel& level,
               const st::ExpansionTerrain& terrain, const terrain::PlanetWindow& w, Report& r) {
    r.add("zoom: %d x %d cells from the column %d and the row %d\n", w.width, w.height, w.i0, w.j0);
    return terrain::writePng(o.directory / "ridges_zoom.png",
                             terrain::renderRidgesZoom(level, terrain, w)) &&
           terrain::writePng(o.directory / "compartments_zoom.png",
                             terrain::renderCompartmentsZoom(level, terrain, w));
}

bool writeReport(const Options& o, const Report& r) {
    std::ofstream out(o.directory / "settlement.txt", std::ios::binary | std::ios::trunc);
    out << r.text();
    return static_cast<bool>(out);
}

int run(const Options& o) {
    terrain::validate(o.world);
    process_memory::returnFreedBlocks();
    Report r;
    const st::SettlementData data = st::loadSettlementData(o.data);
    if (!o.preset.empty() && o.preset != "all") {
        static_cast<void>(st::resolvePreset(data.config, {o.preset, {}, {}, {}}, o.seed, 1.0));
    }
    terrain::PlanetParams params;
    params.cellSize = static_cast<double>(o.cell);
    params.threads = o.threads;
    auto start = std::chrono::steady_clock::now();
    const terrain::Heightfield field(o.seed, terrain::HeightfieldParams{}, o.world);
    const terrain::PlanetLevel level = openLevel(o, params);
    r.add("planet level %d x %d cells of %.0f m in %.1f s, checksum %016llx, %.0f MiB resident\n",
          level.grid().nx, level.grid().nz, level.grid().cellSize, since(start),
          static_cast<unsigned long long>(level.checksum()),
          mebibytes(process_memory::resident()));
    start = std::chrono::steady_clock::now();
    const st::SettlementMaps maps =
        st::computeSettlementMaps(level, &field, data, o.seed, o.threads);
    const st::SettlementMapTimings& t = maps.timings;
    r.add("settlement maps in %.1f s (inputs %.1f, distances %.1f, geology %.1f, network %.1f, "
          "cells %.1f), %.0f MiB of maps, %.0f MiB resident, checksum %016llx\n",
          since(start), t.inputs, t.distances, t.geology, t.network, t.cells,
          mebibytes(maps.memoryBytes()), mebibytes(process_memory::resident()),
          static_cast<unsigned long long>(maps.checksum()));
    settlement_tool::reportMaps(maps, data, r);
    settlement_tool::reportRoughness(field, maps, r);
    if (o.equitySeeds > 0) {
        settlement_tool::reportEquitySeeds(maps, data, o.seed, o.equitySeeds, r);
    }
    const st::SuitabilityMaps suitability =
        st::computeSuitability(level, maps, data, o.seed, o.threads);
    settlement_tool::reportSuitability(suitability, data, r);
    start = std::chrono::steady_clock::now();
    if (!writeImages(o, level, maps, data, suitability)) {
        std::fprintf(stderr, "Cannot write the images into %s\n", o.directory.string().c_str());
        return EXIT_FAILURE;
    }
    r.add("images in %.1f s\n", since(start));
    std::vector<std::string> presets{o.preset.empty() ? data.config.defaultPreset : o.preset};
    if (o.preset == "all") {
        presets.clear();
        for (const st::WorldPreset& p : data.config.presets) {
            presets.push_back(p.id);
        }
    }
    start = std::chrono::steady_clock::now();
    const st::ExpansionTerrain terrain =
        st::buildExpansionTerrain(level, maps, data.config, o.seed, o.threads);
    settlement_tool::reportTerrain(terrain, since(start), r);
    settlement_tool::reportRidges(level, terrain, r);
    settlement_tool::reportCuts(st::climateCuts(level, *maps.land, data.config.borders), r);
    const terrain::PlanetWindow window =
        terrain::planetWindow(level.grid(), o.zoomX, o.zoomZ, o.zoomWidthKm, o.zoomHeightKm);
    if (o.zoom && !writeZoom(o, level, terrain, window, r)) {
        return EXIT_FAILURE;
    }
    for (const std::string& preset : presets) {
        if (!settlement_tool::runPreset(level, maps, suitability, terrain, data,
                                        {preset, o.seed, o.threads, o.directory, o.width,
                                         o.zoom ? &window : nullptr},
                                        r)) {
            std::fprintf(stderr, "Cannot write the files of %s into %s\n", preset.c_str(),
                         o.directory.string().c_str());
            return EXIT_FAILURE;
        }
    }
    r.add("peak resident %.0f MiB\n", mebibytes(process_memory::peakResident()));
    return writeReport(o, r) ? EXIT_SUCCESS : EXIT_FAILURE;
}

void printUsage(const char* program) {
    std::fprintf(stderr,
                 "Usage: %s <output-dir> [--planet <file>] [--rebuild-planet] [--seed <seed>] "
                 "[--world <x> <z>] [--cell <meters>] [--threads <n>] [--data <dir>] "
                 "[--width <pixels>] [--equity-seeds <n>] [--preset <id>|all] "
                 "[--zoom <x> <z> <width-km> <height-km>]\n",
                 program);
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }
    try {
        return run(o);
    } catch (const std::exception& e) {
        const bool clear = std::strncmp(e.what(), "Invalid", 7) == 0;
        std::fprintf(stderr, "%s%s\n", clear ? "" : "Invalid input: ", e.what());
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }
}
