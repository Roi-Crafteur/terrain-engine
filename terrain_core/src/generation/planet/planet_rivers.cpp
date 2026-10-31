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

// The inputs of a river cell: the river and lake cells feeding it (bits 0-1, at most 2: only 0, 1
// and more matter), and kLakeInput when a lake feeds it.
constexpr std::uint8_t kInputCount = 0x03;
constexpr std::uint8_t kLakeInput = 0x80;

void addInput(std::uint8_t& inputs, bool lake) {
    const auto count = static_cast<std::uint8_t>(std::min(2, (inputs & kInputCount) + 1));
    inputs = static_cast<std::uint8_t>((inputs & kLakeInput) | count | (lake ? kLakeInput : 0));
}

class GraphBuilder {
public:
    GraphBuilder(const PlanetGrid& grid, const std::vector<float>& height, const LandIndex& land,
                 const LandValues<std::uint8_t>& receiver, const LakeSolution& lakes,
                 const PlanetRiverParams& params)
        : grid_(grid), height_(height), land_(land), receiver_(receiver), lakes_(lakes),
          params_(params), pits_(playaPits(lakes.lakes)) {}

    void markRivers(const PlanetFlow& flow, const WaterOf& water);
    void computeOrders(const std::vector<std::uint32_t>& order);
    void traceSegments();
    RiverGraph release() { return std::move(graph_); }

private:
    [[nodiscard]] bool river(std::size_t cell) const { return rivers_.land(cell); }
    [[nodiscard]] std::size_t riverSlot(std::size_t cell) const { return rivers_.slot(cell); }
    [[nodiscard]] float discharge(std::size_t cell) const { return discharge_[riverSlot(cell)]; }
    void feed(std::size_t cell, std::uint8_t order);
    // A segment starts at a river cell without exactly one river cell above it, except at a dry
    // pit: the rivers reaching one each end there.
    [[nodiscard]] bool isHead(std::size_t cell) const {
        const std::uint8_t in = inputs_[riverSlot(cell)];
        return !((in & kInputCount) == 1 && (in & kLakeInput) == 0) &&
               receiver_[cell] != kNoNeighbor;
    }
    [[nodiscard]] std::int32_t segmentOf(std::size_t head) const;
    RiverVertex vertex(std::size_t cell, const RiverVertex* previous, float elevation,
                       float discharge) const;
    void trace(std::size_t head, RiverSegment& segment) const;
    void close(std::size_t last, std::size_t next, RiverSegment& segment) const;

    const PlanetGrid& grid_;
    const std::vector<float>& height_;
    const LandIndex& land_;
    const LandValues<std::uint8_t>& receiver_;
    const LakeSolution& lakes_;
    const PlanetRiverParams& params_;
    CellLakes pits_;
    CellLakes outlets_;              // (partner cell, overflowing lake), sorted
    LandIndex rivers_;               // the river cells
    std::vector<float> discharge_;   // by river slot
    std::vector<std::uint8_t> inputs_; // by river slot
    std::vector<OrderInputs> cellOrders_; // by river slot
    std::vector<OrderInputs> lakeOrders_;
    std::vector<std::uint32_t> heads_;
    RiverGraph graph_;
};

void GraphBuilder::markRivers(const PlanetFlow& flow, const WaterOf& water) {
    std::vector<std::uint32_t> cells;
    land_.forEachLand([&](std::size_t c, std::size_t slot) {
        if (!lakes_.lakeOfCell.has(c) && flow.discharge.values()[slot] >= params_.minDischarge &&
            !water(c).frozen) {
            cells.push_back(static_cast<std::uint32_t>(c));
        }
    });
    const LandIndex candidates = LandIndex::ofCells(grid_.cells(), cells);
    std::vector<std::uint8_t> inputs(cells.size(), 0);
    for (const std::uint32_t c : cells) {
        if (receiver_[c] != kNoNeighbor) {
            const std::size_t next = grid_.neighbor(c, receiver_[c]);
            if (candidates.land(next)) {
                addInput(inputs[candidates.slot(next)], false);
            }
        }
    }
    for (std::size_t l = 0; l < lakes_.lakes.size(); ++l) {
        const PlanetLake& lake = lakes_.lakes[l];
        if (lake.overflowing && lake.outflow > 0 && candidates.land(lake.partner)) {
            addInput(inputs[candidates.slot(lake.partner)], true);
            outlets_.emplace_back(lake.partner, static_cast<std::int32_t>(l));
        }
    }
    std::sort(outlets_.begin(), outlets_.end());
    // A dry pit fed by no river is no river either: a segment needs at least one step.
    std::vector<std::uint32_t> kept;
    kept.reserve(cells.size());
    discharge_.reserve(cells.size());
    for (std::size_t k = 0; k < cells.size(); ++k) {
        if (!(receiver_[cells[k]] == kNoNeighbor && (inputs[k] & kInputCount) == 0)) {
            kept.push_back(cells[k]);
            inputs[kept.size() - 1] = inputs[k];
            discharge_.push_back(flow.discharge[cells[k]]);
        }
    }
    inputs.resize(kept.size());
    inputs_ = std::move(inputs);
    rivers_ = LandIndex::ofCells(grid_.cells(), kept);
}

void GraphBuilder::feed(std::size_t cell, std::uint8_t order) {
    if (!land_.land(cell)) {
        return;
    }
    if (const std::int32_t lake = lakes_.lakeOfCell[cell]; lake >= 0) {
        lakeOrders_[static_cast<std::size_t>(lake)].add(order);
    } else if (river(cell)) {
        cellOrders_[riverSlot(cell)].add(order);
    }
}

void GraphBuilder::computeOrders(const std::vector<std::uint32_t>& order) {
    cellOrders_.assign(rivers_.landCells(), OrderInputs{});
    lakeOrders_.assign(lakes_.lakes.size(), OrderInputs{});
    graph_.lakeStrahler.assign(lakes_.lakes.size(), 1);
    const std::vector<std::int32_t> events = outflowsByPass(height_, lakes_.lakes);
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
            feed(grid_.neighbor(cell, receiver_[cell]), cellOrders_[riverSlot(cell)].order());
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
    segment.strahler = cellOrders_[riverSlot(head)].order();
    for (auto it = std::lower_bound(outlets_.begin(), outlets_.end(),
                                    std::pair{static_cast<std::uint32_t>(head), std::int32_t{-1}});
         it != outlets_.end() && it->first == head; ++it) {
        segment.sourceLakes.push_back(it->second);
    }
    std::size_t cell = head;
    while (true) {
        segment.vertices.push_back(
            vertex(cell, segment.vertices.empty() ? nullptr : &segment.vertices.back(),
                   height_[cell], discharge(cell)));
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
    const float leaving = discharge(last);
    float elevation = height_[next];
    if (river(next)) {
        segment.mouth = RiverMouth::Confluence;
        segment.downstream = segmentOf(next);
        segment.vertices.push_back(
            vertex(next, &segment.vertices.back(), elevation, discharge(next)));
        return;
    }
    if (!land_.land(next)) {
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
    segment.vertices.push_back(vertex(next, &segment.vertices.back(), elevation, leaving));
}

void GraphBuilder::traceSegments() {
    rivers_.forEachLand([this](std::size_t c, std::size_t) {
        if (isHead(c)) {
            heads_.push_back(static_cast<std::uint32_t>(c));
        }
    });
    graph_.segments.resize(heads_.size());
    for (std::size_t s = 0; s < heads_.size(); ++s) {
        RiverSegment& segment = graph_.segments[s];
        trace(heads_[s], segment);
        segment.vertices.shrink_to_fit(); // no capacity doubled in the graph kept
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
                           const std::shared_ptr<const LandIndex>& land,
                           const LandValues<std::uint8_t>& receiver,
                           const std::vector<std::uint32_t>& order, const LakeSolution& lakes,
                           const PlanetFlow& flow, const WaterOf& water,
                           const PlanetRiverParams& params, const RiverRelease& release) {
    GraphBuilder builder(grid, height, *land, receiver, lakes, params);
    builder.markRivers(flow, water);
    if (release.flow) {
        release.flow();
    }
    builder.computeOrders(order);
    if (release.order) {
        release.order();
    }
    builder.traceSegments();
    return builder.release();
}

} // namespace terrain::planet
