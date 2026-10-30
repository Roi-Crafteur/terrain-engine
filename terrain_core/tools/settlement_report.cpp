#include "settlement_report.hpp"

#include "terrain/generation/settlement/geology.hpp"
#include "terrain/generation/settlement/telluric.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

namespace st = terrain::settlement;

namespace settlement_tool {

void Report::add(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    std::fputs(line, stdout);
    std::fflush(stdout);
    text_ += line;
}

double mebibytes(std::size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

namespace {

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
          m.network.lines.size(), majorLines,
          m.network.lines.size() - static_cast<std::size_t>(majorLines), m.network.nodes.size(),
          crossings, visible, m.network.deadZones.size());
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
    r.add("  %-26s mean %.3f, above 0 on %5.1f %% of the land, at least 0.5 on %5.1f %%\n", name,
          sum / n / 255.0, 100.0 * positive / n, 100.0 * high / n);
}

// Counts of the land cells of each terrain model, and their share of the land.
std::vector<double> landShares(const st::SettlementMaps& m, std::size_t models) {
    std::vector<double> land(models, 0.0);
    for (const std::uint8_t k : m.terrainModel.values()) {
        land[k] += 1.0;
    }
    for (double& s : land) {
        s /= static_cast<double>(m.terrainModel.values().size());
    }
    return land;
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

// Adds the land nodes and line points of a network to their terrain models.
void countNetwork(const st::SettlementMaps& m, const st::TelluricNetwork& n,
                  std::vector<double>& nodes, std::vector<double>& lines) {
    for (const st::ManaNode& node : n.nodes) {
        const std::size_t cell = m.cellAt(node.x, node.z);
        if (m.land->land(cell)) {
            nodes[m.terrainModel[cell]] += 1.0;
        }
    }
    for (const st::TelluricLine& l : n.lines) {
        for (std::size_t p = 0; p < l.x.size(); ++p) {
            const std::size_t cell = m.cellAt(l.x[p], l.z[p]);
            if (m.land->land(cell)) {
                lines[m.terrainModel[cell]] += 1.0;
            }
        }
    }
}

double total(const std::vector<double>& v) {
    return std::accumulate(v.begin(), v.end(), 0.0);
}

} // namespace

void reportMaps(const st::SettlementMaps& m, const st::SettlementData& data, Report& r) {
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
    const std::size_t models = data.config.terrainModels.size();
    const std::vector<double> land = landShares(m, models);
    std::vector<double> nodes(models, 0.0);
    std::vector<double> lines(models, 0.0);
    countNetwork(m, m.network, nodes, lines);
    r.add("the network of this world (share of the land / of the line points / of the %.0f land "
          "nodes):\n",
          total(nodes));
    for (std::size_t k = 0; k < models; ++k) {
        r.add("  %-10s %5.1f %%  %5.1f %%  %5.1f %%\n", data.config.terrainModels[k].id.c_str(),
              100.0 * land[k], 100.0 * lines[k] / std::max(1.0, total(lines)),
              100.0 * nodes[k] / std::max(1.0, total(nodes)));
    }
}

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

void reportEquitySeeds(const st::SettlementMaps& m, const st::SettlementData& data,
                       std::uint64_t seed, int count, Report& r) {
    const std::size_t models = data.config.terrainModels.size();
    const std::vector<double> land = landShares(m, models);
    std::vector<double> nodes(models, 0.0);
    std::vector<double> lines(models, 0.0);
    const terrain::WorldSize world{m.grid.periodX(), m.grid.periodZ()};
    const double scale = st::worldScale(m.grid, data.config);
    for (int s = 0; s < count; ++s) {
        countNetwork(m,
                     st::buildTelluricNetwork(world, seed + static_cast<std::uint64_t>(s),
                                              data.config.magic, scale),
                     nodes, lines);
    }
    r.add("equity over the networks of %d seeds (from %llu): %.0f land nodes, %.0f line points\n",
          count, static_cast<unsigned long long>(seed), total(nodes), total(lines));
    r.add("  %-10s %7s %7s %7s %9s %9s\n", "model", "land", "nodes", "lines", "nodes-land",
          "lines-land");
    int strays = 0;
    for (std::size_t k = 0; k < models; ++k) {
        const double n = nodes[k] / std::max(1.0, total(nodes));
        const double l = lines[k] / std::max(1.0, total(lines));
        const bool counted = land[k] >= 0.05;
        const bool stray = counted && (std::abs(n - land[k]) > 0.03 || std::abs(l - land[k]) > 0.03);
        strays += stray ? 1 : 0;
        r.add("  %-10s %6.1f%% %6.1f%% %6.1f%% %+8.1f %+9.1f%s\n",
              data.config.terrainModels[k].id.c_str(), 100.0 * land[k], 100.0 * n, 100.0 * l,
              100.0 * (n - land[k]), 100.0 * (l - land[k]), stray ? "  <- more than 3 points" : "");
    }
    r.add("  %d model(s) of at least 5 %% of the land beyond 3 points\n", strays);
}

void reportSuitability(const st::SuitabilityMaps& s, const st::SettlementData& data, Report& r) {
    r.add("raceSuitability of %zu peoples in %.1f s, %.0f MiB, checksum %016llx:\n",
          s.byPeople.size(), s.seconds, mebibytes(s.memoryBytes()),
          static_cast<unsigned long long>(s.checksum()));
    for (std::size_t k = 0; k < s.byPeople.size(); ++k) {
        reportMap(data.peoples[k].id.c_str(), s.byPeople[k], r);
    }
}

} // namespace settlement_tool
