#include "terrain/generation/planet/planet_rivers.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace terrain::planet {

namespace {

using CellLakes = std::vector<std::pair<std::uint32_t, std::int32_t>>;

// Running Strahler order of the inputs of a cell or a lake: the highest order and how many
// inputs have it.
struct OrderInputs {
    std::uint8_t best = 0;
    std::uint8_t count = 0;

    void add(std::uint8_t order) {
        if (order > best) {
            best = order;
            count = 1;
        } else if (order == best && count < 255) {
            ++count;
        }
    }
    [[nodiscard]] std::uint8_t order() const {
        if (best == 0) {
            return 1;
        }
        return count >= 2 && best < 255 ? static_cast<std::uint8_t>(best + 1) : best;
    }
};

class GraphBuilder {
public:
    GraphBuilder(const PlanetGrid& grid, const std::vector<float>& height,
                 const std::vector<std::uint8_t>& ocean, const std::vector<std::uint8_t>& receiver,
                 const LakeSolution& lakes, const PlanetFlow& flow, const WaterOf& water,
                 const PlanetRiverParams& params)
        : grid_(grid), height_(height), ocean_(ocean), receiver_(receiver), lakes_(lakes),
          flow_(flow), water_(water), params_(params), pits_(playaPits(lakes.lakes)) {}

    void markRivers();
    void computeOrders(const std::vector<std::uint32_t>& order);
    void traceSegments();
    RiverGraph release() { return std::move(graph_); }

private:
    [[nodiscard]] bool river(std::size_t cell) const { return river_[cell] != 0; }
    void feed(std::size_t cell, std::uint8_t order);
    // A segment starts at a river cell without exactly one river cell above it, except at a dry
    // pit: the rivers reaching one each end there.
    [[nodiscard]] bool isHead(std::size_t cell) const {
        return !(inputs_[cell] == 1 && lakeInputs_[cell] == 0) && receiver_[cell] != kNoNeighbor;
    }
    [[nodiscard]] std::int32_t segmentOf(std::size_t head) const;
    RiverVertex vertex(std::size_t cell, const RiverVertex* previous, float elevation,
                       float discharge) const;
    void trace(std::size_t head, RiverSegment& segment) const;
    void close(std::size_t last, std::size_t next, RiverSegment& segment) const;

    const PlanetGrid& grid_;
    const std::vector<float>& height_;
    const std::vector<std::uint8_t>& ocean_;
    const std::vector<std::uint8_t>& receiver_;
    const LakeSolution& lakes_;
    const PlanetFlow& flow_;
    const WaterOf& water_;
    const PlanetRiverParams& params_;
    CellLakes pits_;
    CellLakes outlets_; // (partner cell, overflowing lake), sorted
    std::vector<std::uint8_t> river_;
    std::vector<std::uint8_t> inputs_;
    std::vector<std::uint8_t> lakeInputs_;
    std::vector<OrderInputs> cellOrders_;
    std::vector<OrderInputs> lakeOrders_;
    std::vector<std::uint32_t> heads_;
    RiverGraph graph_;
};

void GraphBuilder::markRivers() {
    river_.assign(grid_.cells(), 0);
    inputs_.assign(grid_.cells(), 0);
    lakeInputs_.assign(grid_.cells(), 0);
    for (std::size_t c = 0; c < grid_.cells(); ++c) {
        if (ocean_[c] == kLand && lakes_.lakeOfCell[c] < 0 &&
            flow_.discharge[c] >= params_.minDischarge && !water_(c).frozen) {
            river_[c] = 1;
        }
    }
    for (std::size_t c = 0; c < grid_.cells(); ++c) {
        if (river(c) && receiver_[c] != kNoNeighbor) {
            const std::size_t next = grid_.neighbor(c, receiver_[c]);
            inputs_[next] = static_cast<std::uint8_t>(inputs_[next] + (river(next) ? 1 : 0));
        }
    }
    for (std::size_t l = 0; l < lakes_.lakes.size(); ++l) {
        const PlanetLake& lake = lakes_.lakes[l];
        if (lake.overflowing && lake.outflow > 0 && river(lake.partner)) {
            ++inputs_[lake.partner];
            ++lakeInputs_[lake.partner];
            outlets_.emplace_back(lake.partner, static_cast<std::int32_t>(l));
        }
    }
    std::sort(outlets_.begin(), outlets_.end());
    // A dry pit fed by no river is no river either: a segment needs at least one step.
    for (std::size_t c = 0; c < grid_.cells(); ++c) {
        if (river(c) && receiver_[c] == kNoNeighbor && inputs_[c] == 0) {
            river_[c] = 0;
        }
    }
}

void GraphBuilder::feed(std::size_t cell, std::uint8_t order) {
    if (ocean_[cell] != kLand) {
        return;
    }
    if (const std::int32_t lake = lakes_.lakeOfCell[cell]; lake >= 0) {
        lakeOrders_[static_cast<std::size_t>(lake)].add(order);
    } else if (river(cell)) {
        cellOrders_[cell].add(order);
    }
}

void GraphBuilder::computeOrders(const std::vector<std::uint32_t>& order) {
    cellOrders_.assign(grid_.cells(), OrderInputs{});
    lakeOrders_.assign(lakes_.lakes.size(), OrderInputs{});
    graph_.lakeStrahler.assign(lakes_.lakes.size(), 1);
    const std::vector<std::int32_t> events = outflowsByPass(order, lakes_.lakes);
    std::size_t next = 0;
    for (std::size_t p = order.size(); p-- > 0;) {
        const std::uint32_t cell = order[p];
        for (; next < events.size() &&
               lakes_.lakes[static_cast<std::size_t>(events[next])].pass == cell;
             ++next) {
            const auto l = static_cast<std::size_t>(events[next]);
            graph_.lakeStrahler[l] = lakeOrders_[l].order();
            if (lakes_.lakes[l].outflow > 0) {
                feed(lakes_.lakes[l].partner, graph_.lakeStrahler[l]);
            }
        }
        if (river(cell) && receiver_[cell] != kNoNeighbor) {
            feed(grid_.neighbor(cell, receiver_[cell]), cellOrders_[cell].order());
        }
    }
    for (std::size_t l = 0; l < lakes_.lakes.size(); ++l) {
        if (!lakes_.lakes[l].overflowing) {
            graph_.lakeStrahler[l] = lakeOrders_[l].order();
        }
    }
}

std::int32_t GraphBuilder::segmentOf(std::size_t head) const {
    const auto it =
        std::lower_bound(heads_.begin(), heads_.end(), static_cast<std::uint32_t>(head));
    return static_cast<std::int32_t>(it - heads_.begin());
}

RiverVertex GraphBuilder::vertex(std::size_t cell, const RiverVertex* previous, float elevation,
                                 float discharge) const {
    RiverVertex v;
    v.cell = static_cast<std::uint32_t>(cell);
    v.x = grid_.centerX(grid_.column(cell));
    v.z = grid_.centerZ(grid_.row(cell));
    if (previous != nullptr) {
        v.x = nearestImage(v.x, previous->x, grid_.periodX());
        v.z = nearestImage(v.z, previous->z, grid_.periodZ());
    }
    v.elevation = elevation;
    v.discharge = discharge;
    v.width = riverWidth(discharge, params_);
    v.depth = riverDepth(discharge, params_);
    return v;
}

void GraphBuilder::trace(std::size_t head, RiverSegment& segment) const {
    segment.strahler = cellOrders_[head].order();
    for (auto it = std::lower_bound(outlets_.begin(), outlets_.end(),
                                    std::pair{static_cast<std::uint32_t>(head), std::int32_t{-1}});
         it != outlets_.end() && it->first == head; ++it) {
        segment.sourceLakes.push_back(it->second);
    }
    std::size_t cell = head;
    while (true) {
        segment.vertices.push_back(
            vertex(cell, segment.vertices.empty() ? nullptr : &segment.vertices.back(),
                   height_[cell], flow_.discharge[cell]));
        if (receiver_[cell] == kNoNeighbor) {
            segment.mouth = RiverMouth::ClosedBasin;
            const auto it =
                std::lower_bound(pits_.begin(), pits_.end(),
                                 std::pair{static_cast<std::uint32_t>(cell), std::int32_t{-1}});
            segment.mouthLake = it != pits_.end() && it->first == cell ? it->second : -1;
            return;
        }
        const std::size_t next = grid_.neighbor(cell, receiver_[cell]);
        if (river(next) && !isHead(next)) {
            cell = next;
            continue;
        }
        close(cell, next, segment);
        return;
    }
}

// The last point of a segment: the confluence, or the sea, lake or ice it reaches.
void GraphBuilder::close(std::size_t last, std::size_t next, RiverSegment& segment) const {
    const float discharge = flow_.discharge[last];
    float elevation = height_[next];
    if (river(next)) {
        segment.mouth = RiverMouth::Confluence;
        segment.downstream = segmentOf(next);
        segment.vertices.push_back(
            vertex(next, &segment.vertices.back(), elevation, flow_.discharge[next]));
        return;
    }
    if (ocean_[next] != kLand) {
        segment.mouth = RiverMouth::Sea;
        elevation = kSeaLevel;
    } else if (const std::int32_t lake = lakes_.lakeOfCell[next]; lake >= 0) {
        const PlanetLake& l = lakes_.lakes[static_cast<std::size_t>(lake)];
        segment.mouth = l.overflowing ? RiverMouth::Lake : RiverMouth::TerminalLake;
        segment.mouthLake = lake;
        elevation = l.level;
    } else {
        segment.mouth = RiverMouth::Ice; // frozen ground (the only dry land a river can reach)
    }
    segment.vertices.push_back(vertex(next, &segment.vertices.back(), elevation, discharge));
}

void GraphBuilder::traceSegments() {
    for (std::size_t c = 0; c < grid_.cells(); ++c) {
        if (river(c) && isHead(c)) {
            heads_.push_back(static_cast<std::uint32_t>(c));
        }
    }
    graph_.segments.resize(heads_.size());
    for (std::size_t s = 0; s < heads_.size(); ++s) {
        RiverSegment& segment = graph_.segments[s];
        trace(heads_[s], segment);
        for (std::size_t v = 1; v < segment.vertices.size(); ++v) {
            segment.length += dmath::hypot(segment.vertices[v].x - segment.vertices[v - 1].x,
                                           segment.vertices[v].z - segment.vertices[v - 1].z);
        }
    }
    for (std::size_t s = 0; s < graph_.segments.size(); ++s) {
        const std::int32_t down = graph_.segments[s].downstream;
        if (down >= 0) {
            graph_.segments[static_cast<std::size_t>(down)].upstream.push_back(
                static_cast<std::int32_t>(s));
        }
    }
}

} // namespace

float riverWidth(double discharge, const PlanetRiverParams& params) {
    return static_cast<float>(params.widthCoefficient * std::sqrt(std::max(discharge, 0.0)));
}

float riverDepth(double discharge, const PlanetRiverParams& params) {
    return static_cast<float>(params.depthCoefficient *
                              dmath::pow(std::max(discharge, 0.0), params.depthExponent));
}

RiverGraph buildRiverGraph(const PlanetGrid& grid, const std::vector<float>& height,
                           const std::vector<std::uint8_t>& ocean,
                           const std::vector<std::uint8_t>& receiver,
                           const std::vector<std::uint32_t>& order, const LakeSolution& lakes,
                           const PlanetFlow& flow, const WaterOf& water,
                           const PlanetRiverParams& params) {
    GraphBuilder builder(grid, height, ocean, receiver, lakes, flow, water, params);
    builder.markRivers();
    builder.computeOrders(order);
    builder.traceSegments();
    return builder.release();
}

} // namespace terrain::planet
