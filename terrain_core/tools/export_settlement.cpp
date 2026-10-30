// The settlement V1 of a world (docs/PEUPLEMENT.md): its analysis maps on the planet level (read
// from --planet, or built and written there; built in memory without it), written as PNG control
// images (settlement_*.png, resource_*.png), with a report (settlement.txt, also printed): the
// seconds of each stage, the memory, the counts of the geology and of the magic, the statistics
// of each map, the share of the telluric lines and nodes on each terrain model against its share
// of the land (the equity of the magic network).
// Usage: terrain_export_settlement <output-dir> [--planet <file>] [--seed <seed>]
//        [--world <x> <z>] [--cell <meters>] [--threads <n>] [--data <dir>] [--width <pixels>]

#include "process_memory.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/io/settlement_png.hpp"
#include "terrain/storage/planet_file.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

namespace st = terrain::settlement;

namespace {

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
    std::uint64_t cell = 8192;
    int threads = 0;
    std::filesystem::path data = TERRAIN_SETTLEMENT_DATA;
    int width = 2048;
};

bool parseFlag(int argc, char** argv, int& i, Options& o) {
    const std::string flag = argv[i];
    const bool one = i + 1 < argc;
    if (flag == "--planet" && one) {
        o.planet = argv[++i];
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
    return true;
}

// The report, printed and kept for settlement.txt.
class Report {
public:
    void add(const char* format, ...) {
        char line[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(line, sizeof line, format, args);
        va_end(args);
        std::fputs(line, stdout);
        std::fflush(stdout);
        text_ += line;
    }
    [[nodiscard]] const std::string& text() const { return text_; }

private:
    std::string text_;
};

double mebibytes(std::size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

double since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void reportCounts(const st::SettlementMaps& m, Report& r) {
    int kinds[4] = {0, 0, 0, 0};
    int active = 0;
    int large = 0;
    for (const st::Volcano& v : m.geology.volcanoes) {
        ++kinds[static_cast<int>(v.kind)];
        active += v.active;
        large += v.large;
    }
    int majorCraters = 0;
    for (const st::ArcaneCrater& c : m.geology.craters) {
        majorCraters += c.major;
    }
    int majorLines = 0;
    for (const st::TelluricLine& l : m.network.lines) {
        majorLines += l.major;
    }
    int crossings = 0;
    int visible = 0;
    for (const st::ManaNode& n : m.network.nodes) {
        crossings += n.crossing;
        visible += n.known == st::Known::Visible;
    }
    r.add("volcanoes %zu: %d arcs, %d rifts, %d ridges, %d hotspots; %d active, %d large\n",
          m.geology.volcanoes.size(), kinds[0], kinds[1], kinds[2], kinds[3], active, large);
    r.add("arcane craters %zu (%d major)\n", m.geology.craters.size(), majorCraters);
    r.add("telluric lines %zu (%d major, %zu minor); nodes %zu (%d at crossings, %d visible); "
          "dead zones %zu\n",
          m.network.lines.size(), majorLines, m.network.lines.size() - static_cast<std::size_t>(majorLines),
          m.network.nodes.size(), crossings, visible, m.network.deadZones.size());
}

void reportMap(const char* name, const terrain::planet::LandValues<std::uint8_t>& map, Report& r) {
    double sum = 0.0;
    double positive = 0.0;
    double high = 0.0;
    for (const std::uint8_t v : map.values()) {
        sum += v;
        positive += v > 0 ? 1.0 : 0.0;
        high += v >= 128 ? 1.0 : 0.0;
    }
    const auto n = static_cast<double>(map.values().size());
    r.add("  %-18s mean %.3f, above 0 on %5.1f %% of the land, at least 0.5 on %5.1f %%\n", name,
          sum / n / 255.0, 100.0 * positive / n, 100.0 * high / n);
}

void reportEquity(const terrain::PlanetLevel& level, const st::SettlementMaps& m,
                  const st::SettlementConfig& config, Report& r) {
    const std::size_t models = config.terrainModels.size();
    std::vector<double> land(models, 0.0);
    std::vector<double> lines(models, 0.0);
    std::vector<double> nodes(models, 0.0);
    double allLines = 0.0;
    double allNodes = 0.0;
    for (std::size_t s = 0; s < m.terrainModel.values().size(); ++s) {
        const std::size_t k = m.terrainModel.values()[s];
        land[k] += 1.0;
        if (m.telluric.values()[s] >= 128) {
            lines[k] += 1.0;
            allLines += 1.0;
        }
    }
    for (const st::ManaNode& n : m.network.nodes) {
        const std::size_t cell = m.cellAt(n.x, n.z);
        if (m.land->land(cell)) {
            nodes[m.terrainModel[cell]] += 1.0;
            allNodes += 1.0;
        }
    }
    const auto all = static_cast<double>(m.terrainModel.values().size());
    r.add("equity of the magic network (share of the land / of the line cells / of the %.0f land "
          "nodes):\n", allNodes);
    for (std::size_t k = 0; k < models; ++k) {
        r.add("  %-10s %5.1f %%  %5.1f %%  %5.1f %%\n", config.terrainModels[k].id.c_str(),
              100.0 * land[k] / all, allLines > 0 ? 100.0 * lines[k] / allLines : 0.0,
              allNodes > 0 ? 100.0 * nodes[k] / allNodes : 0.0);
    }
    static_cast<void>(level);
}

void reportMaps(const terrain::PlanetLevel& level, const st::SettlementMaps& m,
                const st::SettlementData& data, Report& r) {
    reportCounts(m, r);
    r.add("maps (one byte per land cell, %zu land cells):\n", m.land->landCells());
    reportMap("roughness", m.roughness, r);
    reportMap("vegetation", m.vegetation, r);
    reportMap("arable", m.arable, r);
    reportMap("food", m.food, r);
    reportMap("danger", m.danger, r);
    reportMap("prana", m.prana, r);
    reportMap("telluric lines", m.telluric, r);
    reportMap("mana", m.mana, r);
    reportMap("habitability", m.habitability, r);
    for (std::size_t k = 0; k < data.resources.size(); ++k) {
        reportMap(("resource " + data.resources[k].id).c_str(), m.resources[k], r);
    }
    reportEquity(level, m, data.config, r);
}

// Ranks of values, the ties averaged.
std::vector<double> ranks(const std::vector<double>& v) {
    std::vector<std::size_t> order(v.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return v[a] < v[b]; });
    std::vector<double> r(v.size());
    for (std::size_t i = 0; i < order.size();) {
        std::size_t j = i;
        while (j + 1 < order.size() && v[order[j + 1]] == v[order[i]]) {
            ++j;
        }
        for (std::size_t k = i; k <= j; ++k) {
            r[order[k]] = 0.5 * static_cast<double>(i + j);
        }
        i = j + 1;
    }
    return r;
}

double spearman(const std::vector<double>& a, const std::vector<double>& b) {
    const std::vector<double> ra = ranks(a);
    const std::vector<double> rb = ranks(b);
    const double mean = 0.5 * static_cast<double>(a.size() - 1);
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    for (std::size_t k = 0; k < a.size(); ++k) {
        ab += (ra[k] - mean) * (rb[k] - mean);
        aa += (ra[k] - mean) * (ra[k] - mean);
        bb += (rb[k] - mean) * (rb[k] - mean);
    }
    return ab / std::sqrt(aa * bb);
}

// The roughness of about 2,000 land cells against the spread of the real heights of 6 x 6
// points in each: how faithful the estimate is.
void reportRoughness(const terrain::Heightfield& field, const st::SettlementMaps& m, Report& r) {
    const terrain::PlanetGrid& grid = m.grid;
    const std::size_t stride = std::max<std::size_t>(1, m.land->landCells() / 2000);
    std::vector<double> estimated;
    std::vector<double> real;
    std::size_t k = 0;
    m.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        if (k++ % stride != 0) {
            return;
        }
        double sum = 0.0;
        double sum2 = 0.0;
        for (int a = 0; a < 36; ++a) {
            const double x = grid.centerX(grid.column(cell)) + (a % 6 - 2.5) / 6.0 * grid.cellSize;
            const double z = grid.centerZ(grid.row(cell)) + (a / 6 - 2.5) / 6.0 * grid.cellSize;
            const double h = field.heightAt(x, z);
            sum += h;
            sum2 += h * h;
        }
        real.push_back(std::sqrt(std::max(0.0, sum2 / 36.0 - (sum / 36.0) * (sum / 36.0))));
        estimated.push_back(m.roughness.values()[slot]);
    });
    r.add("roughness against the spread of the real heights in the cell: Spearman %.3f over %zu "
          "cells\n",
          spearman(estimated, real), real.size());
}

terrain::PlanetLevel openLevel(const Options& o, const terrain::PlanetParams& params) {
    if (o.planet.empty()) {
        return terrain::PlanetLevel(o.seed, terrain::HeightfieldParams{}, params, o.world);
    }
    return terrain::openPlanetLevel(o.planet, o.seed, terrain::HeightfieldParams{}, params,
                                    o.world);
}

int run(const Options& o) {
    terrain::validate(o.world);
    process_memory::returnFreedBlocks();
    Report r;
    const st::SettlementData data = st::loadSettlementData(o.data);
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
    reportMaps(level, maps, data, r);
    reportRoughness(field, maps, r);
    start = std::chrono::steady_clock::now();
    if (!terrain::exportSettlementMapPngs(level, maps, data, o.directory, o.width)) {
        std::fprintf(stderr, "Cannot write the images into %s\n", o.directory.string().c_str());
        return EXIT_FAILURE;
    }
    r.add("images in %.1f s; peak resident %.0f MiB\n", since(start),
          mebibytes(process_memory::peakResident()));
    std::FILE* file = std::fopen((o.directory / "settlement.txt").string().c_str(), "wb");
    if (file == nullptr) {
        return EXIT_FAILURE;
    }
    std::fputs(r.text().c_str(), file);
    std::fclose(file);
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [--planet <file>] [--seed <seed>] [--world <x> <z>] "
                     "[--cell <meters>] [--threads <n>] [--data <dir>] [--width <pixels>]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    try {
        return run(o);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Invalid input: %s\nUsage: %s <output-dir> [options]\n", e.what(),
                     argv[0]);
        return EXIT_FAILURE;
    }
}
