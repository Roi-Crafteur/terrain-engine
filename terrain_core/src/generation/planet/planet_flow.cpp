#include "terrain/generation/planet/planet_flow.hpp"

#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace terrain::planet {

namespace {

using PitLakes = std::vector<std::pair<std::uint32_t, std::int32_t>>;

std::int32_t playaAt(const PitLakes& pits, std::size_t cell) {
    const auto it = std::lower_bound(pits.begin(), pits.end(),
                                     std::pair{static_cast<std::uint32_t>(cell), std::int32_t{-1}});
    return it != pits.end() && it->first == cell ? it->second : -1;
}

// What the land cells have received, then their discharge, by land slot: one float each, which
// holds the bits of the units received (below kBig) until the cell is routed, then its
// discharge. A cell that receives kBig units or more keeps them in big_ until routed. The bits
// are copied as bytes, never read as a float.
class Received {
public:
    explicit Received(std::vector<float>& slots) : slots_(slots) {}

    [[nodiscard]] std::int64_t at(std::size_t slot) const {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &slots_[slot], sizeof(bits));
        return bits == kBig ? big_.at(slot) : static_cast<std::int64_t>(bits);
    }
    void add(std::size_t slot, std::int64_t amount) {
        const std::int64_t total = at(slot) + amount;
        std::uint32_t bits = kBig;
        if (total < static_cast<std::int64_t>(kBig)) {
            bits = static_cast<std::uint32_t>(total);
        } else {
            big_[slot] = total;
        }
        std::memcpy(&slots_[slot], &bits, sizeof(bits));
    }
    void route(std::size_t slot, float discharge) {
        big_.erase(slot);
        slots_[slot] = discharge;
    }

private:
    static constexpr std::uint32_t kBig = 0xFFFFFFFFU;
    std::vector<float>& slots_;
    std::unordered_map<std::size_t, std::int64_t> big_;
};

class Router {
public:
    Router(const PlanetGrid& grid, const LandIndex& land, const LakeSolution& lakes,
           PlanetFlow& flow)
        : grid_(grid), land_(land), lakes_(lakes), flow_(flow), pits_(playaPits(lakes.lakes)),
          received_(flow.discharge.values()) {}

    // Water arriving at a cell: into the sea, its lake, or what it passes on.
    void give(std::size_t cell, std::int64_t amount) {
        if (!land_.land(cell)) {
            flow_.toOcean += amount;
        } else if (const std::int32_t lake = lakes_.lakeOfCell[cell]; lake >= 0) {
            flow_.lakeInflow[static_cast<std::size_t>(lake)] += amount;
        } else {
            received_.add(land_.slot(cell), amount);
        }
    }
    void route(std::size_t cell, std::uint8_t direction, std::int64_t runoff);

private:
    const PlanetGrid& grid_;
    const LandIndex& land_;
    const LakeSolution& lakes_;
    PlanetFlow& flow_;
    PitLakes pits_;
    Received received_;
};

void Router::route(std::size_t cell, std::uint8_t direction, std::int64_t runoff) {
    if (!land_.land(cell)) {
        return;
    }
    if (const std::int32_t lake = lakes_.lakeOfCell[cell]; lake >= 0) {
        flow_.lakeInflow[static_cast<std::size_t>(lake)] += runoff;
        return;
    }
    const std::size_t slot = land_.slot(cell);
    const std::int64_t total = received_.at(slot) + runoff;
    received_.route(slot, static_cast<float>(cubicMeters(total)));
    if (direction != kNoNeighbor) {
        give(grid_.neighbor(cell, direction), total);
    } else if (const std::int32_t playa = playaAt(pits_, cell); playa >= 0) {
        flow_.lakeInflow[static_cast<std::size_t>(playa)] += total;
    }
}

// Follows the receivers (and the lakes spilling on) from a cell to the end of its water.
class BasinTracer {
public:
    BasinTracer(const PlanetGrid& grid, const std::vector<std::uint8_t>& ocean,
                const std::vector<std::uint8_t>& receiver, const LakeAt& lakeAt,
                const std::vector<PlanetLake>& lakes, std::vector<std::int32_t>& basin)
        : grid_(grid), ocean_(ocean), receiver_(receiver), lakeAt_(lakeAt), lakes_(lakes),
          pits_(playaPits(lakes)), basin_(basin), lakeBasin_(lakes.size(), kUnknown) {}

    void trace(std::size_t start);
    [[nodiscard]] std::int32_t lakeBasin(std::size_t lake) const { return lakeBasin_[lake]; }

private:
    static constexpr std::int32_t kUnknown = -2;
    [[nodiscard]] std::int32_t lakeId(std::size_t lake) const {
        return static_cast<std::int32_t>(grid_.cells() + lake);
    }

    const PlanetGrid& grid_;
    const std::vector<std::uint8_t>& ocean_;
    const std::vector<std::uint8_t>& receiver_;
    const LakeAt& lakeAt_;
    const std::vector<PlanetLake>& lakes_;
    PitLakes pits_;
    std::vector<std::int32_t>& basin_;
    std::vector<std::int32_t> lakeBasin_;
    std::vector<std::size_t> path_;
    std::vector<std::size_t> pendingLakes_;
};

void BasinTracer::trace(std::size_t start) {
    path_.clear();
    pendingLakes_.clear();
    std::size_t cell = start;
    std::int32_t id = kNoBasin;
    while (true) {
        if (basin_[cell] != kUnknown) {
            id = basin_[cell];
            break;
        }
        if (const std::int32_t lake = lakeAt_(cell); lake >= 0) {
            const auto l = static_cast<std::size_t>(lake);
            if (lakeBasin_[l] != kUnknown) {
                id = lakeBasin_[l];
                break;
            }
            pendingLakes_.push_back(l);
            if (!lakes_[l].overflowing) {
                id = lakeId(l);
                break;
            }
            cell = lakes_[l].partner;
            if (ocean_[cell] != kLand) {
                id = static_cast<std::int32_t>(cell); // spills straight into the sea
                break;
            }
            continue;
        }
        path_.push_back(cell);
        if (receiver_[cell] == kNoNeighbor) {
            const std::int32_t playa = playaAt(pits_, cell);
            id = playa >= 0 ? lakeId(static_cast<std::size_t>(playa))
                            : static_cast<std::int32_t>(cell);
            break;
        }
        const std::size_t next = grid_.neighbor(cell, receiver_[cell]);
        if (ocean_[next] != kLand) {
            id = static_cast<std::int32_t>(cell);
            break;
        }
        cell = next;
    }
    for (const std::size_t c : path_) {
        basin_[c] = id;
    }
    for (const std::size_t l : pendingLakes_) {
        lakeBasin_[l] = id;
    }
}

} // namespace

std::vector<std::int32_t> outflowsByPass(const std::vector<float>& height,
                                         const std::vector<PlanetLake>& lakes) {
    std::vector<std::int32_t> events;
    for (std::size_t l = 0; l < lakes.size(); ++l) {
        if (lakes[l].overflowing) {
            events.push_back(static_cast<std::int32_t>(l));
        }
    }
    // The later pass in the total order first; several lakes can share a pass.
    std::sort(events.begin(), events.end(), [&](std::int32_t a, std::int32_t b) {
        const std::uint32_t pa = lakes[static_cast<std::size_t>(a)].pass;
        const std::uint32_t pb = lakes[static_cast<std::size_t>(b)].pass;
        return pa != pb ? lowerCell(height, pb, pa) : a < b;
    });
    return events;
}

PitLakes playaPits(const std::vector<PlanetLake>& lakes) {
    PitLakes pits;
    for (std::size_t l = 0; l < lakes.size(); ++l) {
        if (lakes[l].playa) {
            pits.emplace_back(lakes[l].pit, static_cast<std::int32_t>(l));
        }
    }
    std::sort(pits.begin(), pits.end());
    return pits;
}

PlanetFlow routeFlow(const PlanetGrid& grid, const std::vector<float>& height,
                     const std::shared_ptr<const LandIndex>& land,
                     const LandValues<std::uint8_t>& receiver,
                     const std::vector<std::uint32_t>& order, const LakeSolution& lakes,
                     const WaterOf& water) {
    PlanetFlow flow;
    flow.discharge = LandValues<float>(land, 0.0f, 0.0f);
    flow.lakeInflow.assign(lakes.lakes.size(), 0);
    const std::vector<std::int32_t> events = outflowsByPass(height, lakes.lakes);
    Router router(grid, *land, lakes, flow);
    std::size_t next = 0;
    for (std::size_t p = order.size(); p-- > 0;) {
        const std::uint32_t cell = order[p];
        for (; next < events.size() &&
               lakes.lakes[static_cast<std::size_t>(events[next])].pass == cell;
             ++next) {
            const PlanetLake& lake = lakes.lakes[static_cast<std::size_t>(events[next])];
            router.give(lake.partner, lake.outflow);
        }
        if (land->land(cell)) {
            router.route(cell, receiver[cell], water(cell).runoff);
        }
    }
    return flow;
}

std::vector<std::int32_t> computeBasins(const PlanetGrid& grid,
                                        const std::vector<std::uint8_t>& ocean,
                                        const std::vector<std::uint8_t>& receiver,
                                        const LakeAt& lakeAt,
                                        const std::vector<PlanetLake>& lakes) {
    std::vector<std::int32_t> basin(grid.cells(), -2);
    BasinTracer tracer(grid, ocean, receiver, lakeAt, lakes, basin);
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        if (ocean[c] != kLand) {
            basin[c] = kNoBasin;
        }
    }
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        if (basin[c] == -2 && lakeAt(c) < 0) {
            tracer.trace(c);
        }
    }
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        if (const std::int32_t lake = lakeAt(c); lake >= 0) {
            const std::int32_t id = tracer.lakeBasin(static_cast<std::size_t>(lake));
            if (id == -2) {
                tracer.trace(c); // a lake no dry cell drains into
            }
            basin[c] = tracer.lakeBasin(static_cast<std::size_t>(lake));
        }
    }
    return basin;
}

} // namespace terrain::planet
