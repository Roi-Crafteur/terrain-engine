#include "terrain/generation/settlement/expansion.hpp"

#include "../planet/planet_parallel.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr std::uint32_t kSea = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kUnknown = kSea - 1;

// The cost of each land cell and its bits but the ridges: river class, lake, barrier model, ford.
void costsAndBits(const PlanetLevel& level, const SettlementMaps& maps,
                  const SettlementConfig& config, ExpansionTerrain& t, int threads) {
    const ExpansionRule& r = config.expansion;
    std::vector<bool> barrier(config.terrainModels.size(), false);
    for (const std::string& id : r.barrierModels) {
        barrier[static_cast<std::size_t>(config.model(id) - config.terrainModels.data())] = true;
    }
    const PlanetGrid& g = t.grid;
    planet::parallelRows(g.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                const std::size_t cell = g.index(i, j);
                if (!t.land->land(cell)) {
                    continue;
                }
                const std::size_t slot = t.land->slot(cell);
                const std::uint8_t model = maps.terrainModel.values()[slot];
                const double rough = unquantize(maps.roughness.values()[slot]);
                const double cost =
                    config.terrainModels[model].expansionCost +
                    r.altitude.cost * rampScore(level.elevation(cell), r.altitude.ramp) +
                    r.roughness.cost * rampScore(rough, r.roughness.ramp) +
                    r.aridity.cost * rampScore(cellHumidity(level, cell), r.aridity.ramp) +
                    r.danger * unquantize(maps.danger.values()[slot]);
                t.cost[slot] = static_cast<float>(cost);
                const int river = maps.water.values()[slot] & kRiverClassMask;
                const bool lake = (level.cells().bits[cell] & PlanetCells::kLakeBit) != 0;
                t.mask[slot] = static_cast<std::uint8_t>(river | (lake ? kLakeBit : 0) |
                                                         (barrier[model] ? kMountainBit : 0));
                if (river >= 1 && river <= 2) {
                    t.ford[slot] = quantize(std::clamp(1.0 - rough / r.fordMaxRoughness, 0.0, 1.0));
                }
            }
        }
    });
}

// The river cells where two rivers or more flow in.
void confluences(const PlanetLevel& level, ExpansionTerrain& t) {
    std::vector<std::uint8_t> upstream(t.mask.size(), 0);
    t.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        const std::uint8_t k = level.cells().receiver(cell);
        if (t.riverClass(slot) == 0 || k == kNoNeighbor) {
            return;
        }
        const std::size_t down = t.grid.neighbor(cell, k);
        if (t.land->land(down)) {
            const std::size_t s = t.land->slot(down);
            if (t.riverClass(s) > 0 && upstream[s] < 255) {
                ++upstream[s];
            }
        }
    });
    for (std::size_t s = 0; s < upstream.size(); ++s) {
        if (upstream[s] >= 2) {
            t.mask[s] |= kConfluenceBit;
        }
    }
}

// The catchment of every land cell: where its receivers first reach a river segment, a lake or
// the sea; a sink without receiver is a catchment of its own. By land slot.
std::vector<std::uint32_t> catchments(const PlanetLevel& level, const ExpansionTerrain& t) {
    const PlanetCells& cells = level.cells();
    const auto segments = static_cast<std::uint32_t>(level.rivers().segments.size());
    const auto lakes = static_cast<std::uint32_t>(level.lakes().size());
    std::uint32_t sinks = 0;
    std::vector<std::uint32_t> id(t.mask.size(), kUnknown);
    std::vector<std::size_t> path;
    t.land->forEachLand([&](std::size_t start, std::size_t) {
        path.clear();
        std::size_t c = start;
        std::uint32_t found = kUnknown;
        while (found == kUnknown) {
            if (!t.land->land(c)) {
                found = kSea;
                break;
            }
            const std::size_t s = t.land->slot(c);
            const std::uint8_t k = cells.receiver(c);
            if (id[s] != kUnknown) {
                found = id[s];
            } else if (t.riverClass(s) > 0 && cells.riverAt(c) != nullptr) {
                found = static_cast<std::uint32_t>(cells.riverAt(c)->segment);
            } else if ((t.mask[s] & kLakeBit) != 0 && cells.lakeAt(c) >= 0) {
                found = segments + static_cast<std::uint32_t>(cells.lakeAt(c));
            } else if (k == kNoNeighbor || path.size() > t.mask.size()) {
                found = segments + lakes + sinks++;
            }
            path.push_back(c);
            c = k == kNoNeighbor ? c : t.grid.neighbor(c, k);
        }
        for (const std::size_t p : path) {
            if (t.land->land(p)) {
                id[t.land->slot(p)] = found;
            }
        }
    });
    return id;
}

// The lowest floor of the land within radius cells of each land cell, by land slot.
std::vector<float> lowestAround(const PlanetLevel& level, const ExpansionTerrain& t, int radius,
                                int threads) {
    std::vector<float> low(t.mask.size(), 0.0f);
    const PlanetGrid& g = t.grid;
    planet::parallelRows(g.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                const std::size_t cell = g.index(i, j);
                if (!t.land->land(cell)) {
                    continue;
                }
                float m = level.elevation(cell);
                for (int dj = -radius; dj <= radius; ++dj) {
                    for (int di = -radius; di <= radius; ++di) {
                        const std::size_t n = g.index(g.wrapColumn(i + di), g.wrapRow(j + dj));
                        if (t.land->land(n)) {
                            m = std::min(m, level.elevation(n));
                        }
                    }
                }
                low[t.land->slot(cell)] = m;
            }
        }
    });
    return low;
}

// The ridges: the edges east and south of each land cell between two dry cells of different
// catchments, high above the land around, where the geography has relief.
void ridges(const PlanetLevel& level, const SettlementMaps& maps, const BorderRule& rule,
            ExpansionTerrain& t, int threads) {
    const std::vector<std::uint32_t> basin = catchments(level, t);
    const std::vector<float> low = lowestAround(level, t, rule.ridgeWindow, threads);
    const PlanetGrid& g = t.grid;
    const auto dry = [&](std::size_t s) { return (t.mask[s] & (kRiverBits | kLakeBit)) == 0; };
    t.land->forEachLand([&](std::size_t cell, std::size_t a) {
        if (!dry(a)) {
            return;
        }
        const int i = g.column(cell);
        const int j = g.row(cell);
        const std::size_t sides[2] = {g.index(g.wrapColumn(i + 1), j), g.index(i, g.wrapRow(j + 1))};
        for (int k = 0; k < 2; ++k) {
            if (!t.land->land(sides[k])) {
                continue;
            }
            const std::size_t b = t.land->slot(sides[k]);
            const double height = std::min(level.elevation(cell), level.elevation(sides[k])) -
                                  std::min(low[a], low[b]);
            const double relief =
                unquantize(std::max(maps.relief.values()[a], maps.relief.values()[b]));
            if (dry(b) && basin[a] != basin[b] && height >= rule.ridgeHeightM &&
                relief >= rule.ridgeRelief) {
                t.mask[a] |= k == 0 ? kRidgeEastBit : kRidgeSouthBit;
            }
        }
    });
}

// The sea cells at most cells cells from the land.
std::shared_ptr<const planet::LandIndex> straitsOf(const ExpansionTerrain& t, int cells,
                                                   int threads) {
    const PlanetGrid& g = t.grid;
    std::vector<std::vector<std::uint32_t>> rows(static_cast<std::size_t>(g.nz));
    planet::parallelRows(g.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                const std::size_t cell = g.index(i, j);
                bool near = false;
                for (int dj = -cells; dj <= cells && !near && !t.land->land(cell); ++dj) {
                    for (int di = -cells; di <= cells && !near; ++di) {
                        near = di * di + dj * dj <= cells * cells &&
                               t.land->land(g.index(g.wrapColumn(i + di), g.wrapRow(j + dj)));
                    }
                }
                if (near) {
                    rows[static_cast<std::size_t>(j)].push_back(static_cast<std::uint32_t>(cell));
                }
            }
        }
    });
    std::vector<std::uint32_t> sorted;
    for (const auto& row : rows) {
        sorted.insert(sorted.end(), row.begin(), row.end());
    }
    return std::make_shared<const planet::LandIndex>(
        planet::LandIndex::ofCells(g.cells(), sorted));
}

} // namespace

std::size_t ExpansionTerrain::memoryBytes() const {
    return land->memoryBytes() + straits->memoryBytes() + cost.capacity() * sizeof(float) +
           mask.capacity() + ford.capacity() + coastKm.capacity();
}

ExpansionTerrain buildExpansionTerrain(const PlanetLevel& level, const SettlementMaps& maps,
                                       const SettlementConfig& config, int threads) {
    const int workers = planet::resolveThreads(threads);
    ExpansionTerrain t;
    t.grid = maps.grid;
    t.land = maps.land;
    const std::size_t n = maps.land->landCells();
    t.cost.assign(n, 0.0f);
    t.mask.assign(n, 0);
    t.ford.assign(n, 0);
    t.coastKm = maps.coastKm.values();
    costsAndBits(level, maps, config, t, workers);
    confluences(level, t);
    ridges(level, maps, config.borders, t, workers);
    t.straits = config.expansion.sea == SeaCrossing::Straits
                    ? straitsOf(t, config.expansion.straitCells, workers)
                    : std::make_shared<const planet::LandIndex>(
                          planet::LandIndex::ofCells(t.grid.cells(), {}));
    return t;
}

} // namespace terrain::settlement
