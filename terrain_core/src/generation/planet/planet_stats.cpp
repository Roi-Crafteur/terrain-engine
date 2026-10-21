#include "terrain/generation/planet/planet_stats.hpp"

#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_flow.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

namespace terrain::planet {

namespace {

// Longest length from a source to the first point of every segment (Kahn order on the graph of
// segments and lakes: upstream segments and lakes come first).
std::vector<double> upstreamLengths(const RiverGraph& graph, std::size_t lakes) {
    const std::size_t count = graph.segments.size();
    std::vector<double> up(count, 0.0);
    std::vector<double> lakeUp(lakes, 0.0);
    std::vector<std::vector<std::int32_t>> lakeOutlets(lakes);
    std::vector<int> waiting(count, 0);
    std::vector<int> lakeWaiting(lakes, 0);
    for (std::size_t s = 0; s < count; ++s) {
        const RiverSegment& segment = graph.segments[s];
        waiting[s] = static_cast<int>(segment.upstream.size() + segment.sourceLakes.size());
        for (const std::int32_t l : segment.sourceLakes) {
            lakeOutlets[static_cast<std::size_t>(l)].push_back(static_cast<std::int32_t>(s));
        }
        if (segment.mouth == RiverMouth::Lake) {
            ++lakeWaiting[static_cast<std::size_t>(segment.mouthLake)];
        }
    }
    std::vector<std::size_t> queue;
    for (std::size_t s = 0; s < count; ++s) {
        if (waiting[s] == 0) {
            queue.push_back(s);
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const RiverSegment& segment = graph.segments[queue[head]];
        const double end = up[queue[head]] + segment.length;
        const auto release = [&](std::size_t next, double length) {
            up[next] = std::max(up[next], length);
            if (--waiting[next] == 0) {
                queue.push_back(next);
            }
        };
        if (segment.mouth == RiverMouth::Confluence) {
            release(static_cast<std::size_t>(segment.downstream), end);
        } else if (segment.mouth == RiverMouth::Lake) {
            const auto l = static_cast<std::size_t>(segment.mouthLake);
            lakeUp[l] = std::max(lakeUp[l], end);
            if (--lakeWaiting[l] == 0) {
                for (const std::int32_t outlet : lakeOutlets[l]) {
                    release(static_cast<std::size_t>(outlet), lakeUp[l]);
                }
            }
        }
    }
    return up;
}

std::vector<PlanetBasinStat> basinStats(const PlanetLevel& level, std::size_t top) {
    const PlanetGrid& grid = level.grid();
    const std::vector<std::int32_t> basin = level.basins();
    const std::size_t ids = grid.cells() + level.lakes().size();
    std::vector<std::uint32_t> cells(ids, 0);
    std::vector<std::uint32_t> frozen(ids, 0);
    std::vector<std::int64_t> water(ids, 0);
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        if (basin[c] >= 0) {
            const auto b = static_cast<std::size_t>(basin[c]);
            const CellWater w = level.water(c);
            ++cells[b];
            frozen[b] += w.frozen ? 1u : 0u;
            water[b] += w.runoff;
        }
    }
    for (std::size_t l = 0; l < level.lakes().size(); ++l) {
        const PlanetLake& lake = level.lakes()[l];
        const std::int32_t b = basin[lake.pit];
        if (b >= 0 && lake.overflowing) {
            water[static_cast<std::size_t>(b)] -= lake.evaporation;
        }
    }
    std::vector<std::size_t> order;
    for (std::size_t b = 0; b < ids; ++b) {
        if (cells[b] > 0) {
            order.push_back(b);
        }
    }
    std::partial_sort(order.begin(),
                      order.begin() + static_cast<std::ptrdiff_t>(std::min(top, order.size())),
                      order.end(), [&](std::size_t a, std::size_t b) {
                          return cells[a] != cells[b] ? cells[a] > cells[b] : a < b;
                      });
    std::vector<PlanetBasinStat> stats;
    for (std::size_t k = 0; k < std::min(top, order.size()); ++k) {
        const std::size_t b = order[k];
        PlanetBasinStat s;
        s.basin = static_cast<std::int32_t>(b);
        s.area = cells[b] * grid.cellArea();
        s.frozenShare = static_cast<double>(frozen[b]) / cells[b];
        s.sea = b < grid.cells();
        const std::size_t outlet = s.sea ? b : level.lakes()[b - grid.cells()].pit;
        s.discharge = cubicMeters(s.sea ? water[b] : level.lakes()[b - grid.cells()].inflow);
        s.x = grid.centerX(grid.column(outlet));
        s.z = grid.centerZ(grid.row(outlet));
        stats.push_back(s);
    }
    return stats;
}

} // namespace

std::vector<double> landMasses(const PlanetGrid& grid, const std::vector<std::uint8_t>& isOcean) {
    std::vector<std::uint8_t> seen(grid.cells(), 0);
    std::vector<std::size_t> queue;
    std::vector<double> masses;
    for (std::size_t start = 0; start < grid.cells(); ++start) {
        if (isOcean[start] != 0 || seen[start] != 0) {
            continue;
        }
        seen[start] = 1;
        queue.assign(1, start);
        for (std::size_t head = 0; head < queue.size(); ++head) {
            for (int k = 0; k < 8; ++k) {
                const std::size_t n = grid.neighbor(queue[head], k);
                if (isOcean[n] == 0 && seen[n] == 0) {
                    seen[n] = 1;
                    queue.push_back(n);
                }
            }
        }
        masses.push_back(static_cast<double>(queue.size()) * grid.cellArea());
    }
    std::stable_sort(masses.begin(), masses.end(), std::greater<>());
    return masses;
}

std::vector<PlanetRiverStat> longestRivers(const RiverGraph& graph, std::size_t top) {
    const std::vector<double> up = upstreamLengths(graph, graph.lakeStrahler.size());
    std::vector<std::size_t> outlets(graph.lakeStrahler.size(), 0);
    for (const RiverSegment& segment : graph.segments) {
        for (const std::int32_t l : segment.sourceLakes) {
            ++outlets[static_cast<std::size_t>(l)];
        }
    }
    std::vector<PlanetRiverStat> rivers;
    for (std::size_t s = 0; s < graph.segments.size(); ++s) {
        const RiverSegment& segment = graph.segments[s];
        const bool goesOn = segment.mouth == RiverMouth::Confluence ||
                            (segment.mouth == RiverMouth::Lake &&
                             outlets[static_cast<std::size_t>(segment.mouthLake)] > 0);
        if (goesOn) {
            continue;
        }
        const RiverVertex& last =
            segment.vertices[segment.vertices.size() -
                             (segment.mouth == RiverMouth::ClosedBasin ? 1 : 2)];
        rivers.push_back({static_cast<std::int32_t>(s), up[s] + segment.length, last.discharge,
                          segment.mouth, segment.strahler, last.x, last.z});
    }
    std::stable_sort(rivers.begin(), rivers.end(),
                     [](const auto& a, const auto& b) { return a.length > b.length; });
    rivers.resize(std::min(top, rivers.size()));
    return rivers;
}

PlanetStats planetStats(const PlanetLevel& level, std::size_t top) {
    const PlanetGrid& grid = level.grid();
    PlanetStats stats;
    std::vector<std::uint8_t> ocean(grid.cells());
    std::size_t land = 0;
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        ocean[c] = level.cells().ocean(c) ? 1 : 0;
        land += ocean[c] == 0 ? 1u : 0u;
    }
    stats.landShare = static_cast<double>(land) / static_cast<double>(grid.cells());
    stats.landMasses = landMasses(grid, ocean);
    stats.largestBasins = basinStats(level, top);
    stats.longestRivers = longestRivers(level.rivers(), top);
    for (const PlanetLake& lake : level.lakes()) {
        ++stats.lakes;
        stats.overflowingLakes += lake.overflowing ? 1u : 0u;
        stats.playas += lake.playa ? 1u : 0u;
        stats.terminalLakes += !lake.overflowing && !lake.playa ? 1u : 0u;
    }
    for (const RiverSegment& segment : level.rivers().segments) {
        stats.riverLength += segment.length;
    }
    return stats;
}

} // namespace terrain::planet
