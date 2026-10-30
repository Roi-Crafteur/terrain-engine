// The settlement on the default world (cells of 65,536 m, settlement_world_support.hpp): the world
// has the variety the checks need; the roughness follows the real relief; the maps and the
// resources are where their rules put them; the magic network is fair to every terrain; the same
// bits whatever the threads.

#include "settlement_world_support.hpp"

#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/generation/settlement/telluric.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

namespace st = terrain::settlement;
using settlement_test::World;

namespace {

int modelIndex(const st::SettlementConfig& config, const std::string& id) {
    for (std::size_t k = 0; k < config.terrainModels.size(); ++k) {
        if (config.terrainModels[k].id == id) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

// Shares of the land per terrain model.
std::vector<double> modelShares(const st::SettlementMaps& m, std::size_t models) {
    std::vector<double> share(models, 0.0);
    for (const std::uint8_t k : m.terrainModel.values()) {
        share[k] += 1.0;
    }
    for (double& s : share) {
        s /= static_cast<double>(m.terrainModel.values().size());
    }
    return share;
}

// The mean of a map over the land cells of a terrain model.
double meanOn(const st::SettlementMaps& m, const terrain::planet::LandValues<std::uint8_t>& map,
              int model) {
    double sum = 0.0;
    double n = 0.0;
    for (std::size_t s = 0; s < map.values().size(); ++s) {
        if (m.terrainModel.values()[s] == model) {
            sum += map.values()[s];
            n += 1.0;
        }
    }
    return n > 0.0 ? sum / n / 255.0 : 0.0;
}

void testVariety(const World& w, const st::SettlementMaps& m) {
    const std::vector<double> share = modelShares(m, w.data.config.terrainModels.size());
    for (std::size_t k = 0; k < share.size(); ++k) {
        std::printf("  %-10s %5.1f %% of the land\n", w.data.config.terrainModels[k].id.c_str(),
                    100.0 * share[k]);
    }
    const auto at = [&](const char* id) {
        return share[static_cast<std::size_t>(modelIndex(w.data.config, id))];
    };
    TEST_CHECK(at("desert") > 0.02 && at("glacier") > 0.02 && at("mountain") > 0.015);
    TEST_CHECK(at("plains") > 0.1 && at("forest") > 0.02);
}

// Ranks of values, ties averaged.
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

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    const double n = static_cast<double>(a.size());
    const double ma = std::accumulate(a.begin(), a.end(), 0.0) / n;
    const double mb = std::accumulate(b.begin(), b.end(), 0.0) / n;
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    for (std::size_t k = 0; k < a.size(); ++k) {
        ab += (a[k] - ma) * (b[k] - mb);
        aa += (a[k] - ma) * (a[k] - ma);
        bb += (b[k] - mb) * (b[k] - mb);
    }
    return ab / std::sqrt(aa * bb);
}

// The roughness of a cell against the spread of the real heights of 6 x 6 points in it.
void testRoughness(const World& w, const st::SettlementMaps& m) {
    const terrain::PlanetGrid& grid = m.grid;
    std::vector<double> estimated;
    std::vector<double> real;
    std::size_t k = 0;
    m.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        if (k++ % 600 != 0 || w.level.cells().lakeAt(cell) >= 0) {
            return;
        }
        double sum = 0.0;
        double sum2 = 0.0;
        for (int a = 0; a < 6; ++a) {
            for (int b = 0; b < 6; ++b) {
                const double x = grid.centerX(grid.column(cell)) + (a - 2.5) / 6.0 * grid.cellSize;
                const double z = grid.centerZ(grid.row(cell)) + (b - 2.5) / 6.0 * grid.cellSize;
                const double h = w.field.heightAt(x, z);
                sum += h;
                sum2 += h * h;
            }
        }
        real.push_back(std::sqrt(std::max(0.0, sum2 / 36.0 - (sum / 36.0) * (sum / 36.0))));
        estimated.push_back(m.roughness.values()[slot]);
    });
    const double rho = correlation(ranks(estimated), ranks(real));
    std::printf("roughness against the real relief: Spearman %.3f over %zu cells\n", rho,
                real.size());
    // 0.86 on these cells of 65 km, 0.92 on the cells of 8 km (terrain_export_settlement).
    TEST_CHECK(real.size() > 200 && rho > 0.75);
}

void testMaps(const World& w, const st::SettlementMaps& m) {
    const st::SettlementConfig& c = w.data.config;
    const int glacier = modelIndex(c, "glacier");
    const int plains = modelIndex(c, "plains");
    const int forest = modelIndex(c, "forest");
    const int desert = modelIndex(c, "desert");
    std::printf("means on glaciers / plains / forests / deserts: arable %.2f %.2f %.2f %.2f, "
                "prana %.2f %.2f %.2f %.2f, habitability %.2f %.2f %.2f %.2f, danger %.2f %.2f "
                "%.2f %.2f\n",
                meanOn(m, m.arable, glacier), meanOn(m, m.arable, plains),
                meanOn(m, m.arable, forest), meanOn(m, m.arable, desert),
                meanOn(m, m.prana, glacier), meanOn(m, m.prana, plains),
                meanOn(m, m.prana, forest), meanOn(m, m.prana, desert),
                meanOn(m, m.habitability, glacier), meanOn(m, m.habitability, plains),
                meanOn(m, m.habitability, forest), meanOn(m, m.habitability, desert),
                meanOn(m, m.danger, glacier), meanOn(m, m.danger, plains),
                meanOn(m, m.danger, forest), meanOn(m, m.danger, desert));
    TEST_CHECK(meanOn(m, m.arable, glacier) == 0.0 && meanOn(m, m.arable, plains) > 0.4);
    TEST_CHECK(meanOn(m, m.prana, forest) > meanOn(m, m.prana, desert) + 0.3);
    TEST_CHECK(meanOn(m, m.prana, forest) > meanOn(m, m.prana, glacier) + 0.3);
    TEST_CHECK(meanOn(m, m.habitability, plains) > meanOn(m, m.habitability, glacier) + 0.3);
    TEST_CHECK(meanOn(m, m.habitability, plains) > meanOn(m, m.habitability, desert) + 0.1);
    TEST_CHECK(meanOn(m, m.danger, glacier) > meanOn(m, m.danger, plains) + 0.3);
}

// Each resource in patches where its rule puts it.
void testResources(const World& w, const terrain::PlanetLevel& level, const st::SettlementMaps& m) {
    for (std::size_t r = 0; r < w.data.resources.size(); ++r) {
        double cells = 0.0;
        double vegetation = 0.0;
        double humidity = 0.0;
        m.land->forEachLand([&](std::size_t cell, std::size_t slot) {
            if (m.resources[r].values()[slot] == 0) {
                return;
            }
            cells += 1.0;
            vegetation += m.vegetation.values()[slot] / 255.0;
            humidity += st::cellHumidity(level, cell);
        });
        const double share = cells / static_cast<double>(m.land->landCells());
        const std::string& id = w.data.resources[r].id;
        std::printf("resource %-17s on %5.2f %% of the land, mean vegetation %.2f, humidity %.2f\n",
                    id.c_str(), 100.0 * share, cells > 0 ? vegetation / cells : 0.0,
                    cells > 0 ? humidity / cells : 0.0);
        TEST_CHECK(cells > 0.0 && share < 0.9);
        if (id == "bois") {
            TEST_CHECK(vegetation / cells > 0.4);
        } else if (id == "sel") {
            TEST_CHECK(humidity / cells < 0.35);
        }
    }
}

// The lines and the nodes fall on each terrain as much as the terrain covers of the land: the
// networks of several seeds over the terrains of this world (a network never reads the terrain;
// a few lines per world are too few for a share on their own).
void testEquity(const World& w, const st::SettlementMaps& m) {
    const std::size_t models = w.data.config.terrainModels.size();
    const std::vector<double> land = modelShares(m, models);
    std::vector<double> lines(models, 0.0);
    std::vector<double> nodes(models, 0.0);
    double allLines = 0.0;
    double allNodes = 0.0;
    const double scale = st::worldScale(m.grid, w.data.config);
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        const st::TelluricNetwork n = st::buildTelluricNetwork(
            w.world, settlement_test::kSeed + seed, w.data.config.magic, scale);
        for (const st::TelluricLine& l : n.lines) {
            for (std::size_t p = 0; p < l.x.size(); ++p) {
                const std::size_t cell = m.cellAt(l.x[p], l.z[p]);
                if (m.land->land(cell)) {
                    lines[m.terrainModel[cell]] += 1.0;
                    allLines += 1.0;
                }
            }
        }
        for (const st::ManaNode& node : n.nodes) {
            const std::size_t cell = m.cellAt(node.x, node.z);
            if (m.land->land(cell)) {
                nodes[m.terrainModel[cell]] += 1.0;
                allNodes += 1.0;
            }
        }
    }
    std::printf("equity over 8 networks: %.0f line points, %.0f nodes on the land\n", allLines,
                allNodes);
    for (std::size_t k = 0; k < models; ++k) {
        const double l = lines[k] / allLines;
        const double n = nodes[k] / allNodes;
        std::printf("  %-10s land %5.1f %%, lines %5.1f %%, nodes %5.1f %%\n",
                    w.data.config.terrainModels[k].id.c_str(), 100.0 * land[k], 100.0 * l,
                    100.0 * n);
        if (land[k] >= 0.05) {
            TEST_CHECK(std::abs(l - land[k]) <= std::max(0.03, 0.3 * land[k]));
            TEST_CHECK(std::abs(n - land[k]) <= std::max(0.03, 0.3 * land[k]));
        }
    }
    TEST_CHECK(allLines > 10000.0 && allNodes > 1000.0);
}

} // namespace

int main() {
    auto start = std::chrono::steady_clock::now();
    const World w;
    std::printf("world built in %.2f s\n", settlement_test::secondsSince(start));
    start = std::chrono::steady_clock::now();
    const st::SettlementMaps m = st::computeSettlementMaps(w.level, &w.field, w.data,
                                                           settlement_test::kSeed, 0);
    std::printf("maps in %.2f s, %zu land cells\n", settlement_test::secondsSince(start),
                m.land->landCells());
    testVariety(w, m);
    testRoughness(w, m);
    testMaps(w, m);
    testResources(w, w.level, m);
    testEquity(w, m);
    const std::uint64_t sum = m.checksum();
    TEST_CHECK(st::computeSettlementMaps(w.level, &w.field, w.data,
                                         settlement_test::kSeed, 1)
                   .checksum() == sum);
    TEST_CHECK(st::computeSettlementMaps(w.level, &w.field, w.data,
                                         settlement_test::kSeed, 3)
                   .checksum() == sum);
    std::printf("total %.2f s\n", settlement_test::secondsSince(start));
    return test::result();
}
