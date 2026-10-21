#include "terrain/generation/planet/planet_stats.hpp"

#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_flow.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

namespace terrain::planet {

namespace {

// Longest paths through the river graph, the lakes included: a river crossing a lake goes
// straight from where it enters to the partner of the lake's pass, and goes on from there in a
// river, in another lake or into the sea, when the lake lets out a river's discharge (else it
// ends in the lake). Kahn order on the segments [0, S) and the lakes [S, S + L): every node after
// all the nodes upstream of it.
struct RiverPath {
    double length = 0.0;
    double lakeLength = 0.0; // the part of it across lakes
};

class RiverLengths {
public:
    RiverLengths(const RiverGraph& graph, const std::vector<PlanetLake>& lakes,
                 const PlanetGrid& grid, const LakeAt& lakeAt,
                 const std::function<bool(std::size_t)>& isOcean, double minDischarge)
        : graph_(graph), lakes_(lakes), grid_(grid), lakeAt_(lakeAt), isOcean_(isOcean),
          minDischarge_(minDischarge), up_(graph.segments.size() + lakes.size()),
          entered_(lakes.size(), false), waiting_(up_.size(), 0), outlets_(lakes.size()) {}

    std::vector<PlanetRiverStat> run();

private:
    [[nodiscard]] std::size_t lakeNode(std::size_t lake) const {
        return graph_.segments.size() + lake;
    }
    [[nodiscard]] double distance(double x0, double z0, std::size_t cell) const {
        const double x = nearestImage(grid_.centerX(grid_.column(cell)), x0, grid_.periodX());
        const double z = nearestImage(grid_.centerZ(grid_.row(cell)), z0, grid_.periodZ());
        return dmath::hypot(x - x0, z - z0);
    }
    // A river entering the lake goes on beyond it.
    [[nodiscard]] bool goesOn(std::size_t lake) const {
        return lakes_[lake].overflowing && cubicMeters(lakes_[lake].outflow) >= minDischarge_;
    }
    [[nodiscard]] std::int32_t nextLake(std::size_t lake) const;
    void count();
    void release(std::size_t node, RiverPath path);
    void enterLake(std::size_t lake, RiverPath path, double x, double z, bool river);
    void segmentDone(std::size_t s);
    void lakeDone(std::size_t lake);
    void end(RiverPath path, double discharge, RiverMouth mouth, std::int32_t segment, double x,
             double z, std::uint8_t strahler);

    const RiverGraph& graph_;
    const std::vector<PlanetLake>& lakes_;
    const PlanetGrid& grid_;
    const LakeAt& lakeAt_;
    const std::function<bool(std::size_t)>& isOcean_;
    double minDischarge_;
    std::vector<RiverPath> up_; // longest path to the start of a segment / the exit of a lake
    std::vector<bool> entered_; // a river reaches the lake
    std::vector<int> waiting_;
    std::vector<std::vector<std::int32_t>> outlets_; // segments starting at each lake's exit
    std::vector<std::size_t> queue_;
    std::vector<PlanetRiverStat> ends_;
};

// The lake a lake spills a river's discharge straight into, or -1.
std::int32_t RiverLengths::nextLake(std::size_t lake) const {
    return goesOn(lake) && outlets_[lake].empty() ? lakeAt_(lakes_[lake].partner) : -1;
}

void RiverLengths::count() {
    for (std::size_t s = 0; s < graph_.segments.size(); ++s) {
        const RiverSegment& segment = graph_.segments[s];
        waiting_[s] = static_cast<int>(segment.upstream.size() + segment.sourceLakes.size());
        for (const std::int32_t l : segment.sourceLakes) {
            outlets_[static_cast<std::size_t>(l)].push_back(static_cast<std::int32_t>(s));
        }
        if (segment.mouth == RiverMouth::Lake || segment.mouth == RiverMouth::TerminalLake) {
            ++waiting_[lakeNode(static_cast<std::size_t>(segment.mouthLake))];
        }
    }
    for (std::size_t l = 0; l < lakes_.size(); ++l) {
        if (const std::int32_t next = nextLake(l); next >= 0) {
            ++waiting_[lakeNode(static_cast<std::size_t>(next))];
        }
    }
}

void RiverLengths::release(std::size_t node, RiverPath path) {
    if (path.length > up_[node].length) {
        up_[node] = path;
    }
    if (--waiting_[node] == 0) {
        queue_.push_back(node);
    }
}

// A river (or a lake spilling) enters a lake at (x, z) at the end of path.
void RiverLengths::enterLake(std::size_t lake, RiverPath path, double x, double z, bool river) {
    const PlanetLake& l = lakes_[lake];
    entered_[lake] = entered_[lake] || river;
    if (l.overflowing) {
        const double across = distance(x, z, l.partner);
        path.length += across;
        path.lakeLength += across;
    }
    release(lakeNode(lake), path);
}

void RiverLengths::end(RiverPath path, double discharge, RiverMouth mouth, std::int32_t segment,
                       double x, double z, std::uint8_t strahler) {
    ends_.push_back({segment, path.length, path.lakeLength, discharge, mouth, strahler, x, z});
}

void RiverLengths::segmentDone(std::size_t s) {
    const RiverSegment& segment = graph_.segments[s];
    const RiverPath path{up_[s].length + segment.length, up_[s].lakeLength};
    const RiverVertex& last = segment.vertices.back();
    const RiverVertex& river =
        segment
            .vertices[segment.vertices.size() - (segment.mouth == RiverMouth::ClosedBasin ? 1 : 2)];
    const auto id = static_cast<std::int32_t>(s);
    switch (segment.mouth) {
    case RiverMouth::Confluence:
        release(static_cast<std::size_t>(segment.downstream), path);
        return;
    case RiverMouth::Lake:
    case RiverMouth::TerminalLake:
        if (!goesOn(static_cast<std::size_t>(segment.mouthLake))) {
            end(path, river.discharge, segment.mouth, id, river.x, river.z, segment.strahler);
        }
        enterLake(static_cast<std::size_t>(segment.mouthLake), path, last.x, last.z, true);
        return;
    default:
        end(path, river.discharge, segment.mouth, id, river.x, river.z, segment.strahler);
    }
}

void RiverLengths::lakeDone(std::size_t lake) {
    const PlanetLake& l = lakes_[lake];
    if (!l.overflowing || l.outflow <= 0) {
        return;
    }
    const RiverPath path = up_[lakeNode(lake)];
    const double x = grid_.centerX(grid_.column(l.partner));
    const double z = grid_.centerZ(grid_.row(l.partner));
    const double outflow = cubicMeters(l.outflow);
    const std::uint8_t order = lake < graph_.lakeStrahler.size() ? graph_.lakeStrahler[lake] : 1;
    const bool onward = entered_[lake] && goesOn(lake);
    for (const std::int32_t s : outlets_[lake]) {
        release(static_cast<std::size_t>(s), onward ? path : RiverPath{});
    }
    if (!outlets_[lake].empty() || !onward) {
        if (const std::int32_t next = nextLake(lake); next >= 0) {
            enterLake(static_cast<std::size_t>(next), RiverPath{}, x, z, false);
        }
        return;
    }
    if (const std::int32_t next = nextLake(lake); next >= 0) {
        const auto n = static_cast<std::size_t>(next);
        if (!goesOn(n)) {
            end(path, outflow, lakes_[n].overflowing ? RiverMouth::Lake : RiverMouth::TerminalLake,
                -1, x, z, order);
        }
        enterLake(n, path, x, z, true);
    } else {
        end(path, outflow, isOcean_(l.partner) ? RiverMouth::Sea : RiverMouth::Lake, -1, x, z,
            order);
    }
}

std::vector<PlanetRiverStat> RiverLengths::run() {
    count();
    for (std::size_t node = 0; node < up_.size(); ++node) {
        if (waiting_[node] == 0) {
            queue_.push_back(node);
        }
    }
    for (std::size_t head = 0; head < queue_.size(); ++head) {
        const std::size_t node = queue_[head];
        if (node < graph_.segments.size()) {
            segmentDone(node);
        } else {
            lakeDone(node - graph_.segments.size());
        }
    }
    return std::move(ends_);
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

std::vector<PlanetRiverStat> longestRivers(const RiverGraph& graph,
                                           const std::vector<PlanetLake>& lakes,
                                           const PlanetGrid& grid, const LakeAt& lakeAt,
                                           const std::function<bool(std::size_t)>& isOcean,
                                           double minDischarge, std::size_t top) {
    std::vector<PlanetRiverStat> rivers =
        RiverLengths(graph, lakes, grid, lakeAt, isOcean, minDischarge).run();
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
    const LakeAt lakeAt = [&level](std::size_t c) { return level.cells().lakeAt(c); };
    const std::function<bool(std::size_t)> isOcean = [&level](std::size_t c) {
        return level.cells().ocean(c);
    };
    stats.longestRivers = longestRivers(level.rivers(), level.lakes(), grid, lakeAt, isOcean,
                                        level.params().rivers.minDischarge, top);
    stats.lakeArea = static_cast<double>(level.cells().lakeCells.size()) * grid.cellArea();
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
