#include "terrain/generation/settlement/expansion.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr double kScale = 256.0; // the costs in fixed point, 1/256
constexpr double kDiagonal = 1.4142135623730951;
constexpr std::uint32_t kFar = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint16_t kNobody = 0x7FFF;
constexpr std::uint16_t kBarrierTag = 0x8000;

struct Entry {
    std::uint64_t key; // the cost, then a hash of (cell, state) that favors no state
    std::uint32_t cell;
    std::uint16_t state;
    bool barrier; // reached across a barrier
};

struct Later {
    bool operator()(const Entry& a, const Entry& b) const {
        if (a.key != b.key) {
            return a.key > b.key;
        }
        return a.cell != b.cell ? a.cell > b.cell : a.state > b.state;
    }
};

// A step to a neighbor: its cell, its node (land slot, or the strait slot after the land), its
// cost, whether it crosses a barrier.
struct Step {
    std::size_t cell = 0;
    std::size_t node = 0;
    double cost = 0.0;
    bool barrier = false;
};

class Expansion {
public:
    Expansion(const ExpansionTerrain& t, const SuitabilityMaps& s, const ExpansionRule& rule,
              const std::vector<StatePlan>& states)
        : t_(t), s_(s), rule_(rule), states_(states), landNodes_(t.land->landCells()),
          best_(landNodes_ + t.straits->landCells(), kFar), tag_(best_.size(), kNobody) {
        out_.owner.assign(landNodes_, 0);
        out_.passage.assign(t.straits->landCells(), 0);
        out_.cells.assign(states.size(), 0);
    }

    ExpansionResult run(const std::vector<std::size_t>& capitals, double landCap) {
        std::vector<double> cap(states_.size());
        for (std::size_t s = 0; s < states_.size(); ++s) {
            cap[s] = std::min(rule_.overflow * states_[s].targetCells, landCap);
            push(capitals[s], t_.land->slot(capitals[s]), static_cast<std::uint16_t>(s), 0, false);
        }
        while (!heap_.empty()) {
            const Entry e = heap_.top();
            heap_.pop();
            const std::size_t node = nodeOf(e.cell);
            const double held = static_cast<double>(out_.cells[e.state]);
            if (claimed(node) || held >= cap[e.state] ||
                (e.barrier && held >= rule_.barrierStop * states_[e.state].targetCells)) {
                continue;
            }
            claim(node, e.state);
            grow(e.cell, e.state, static_cast<std::uint32_t>(e.key >> 32));
        }
        return std::move(out_);
    }

private:
    [[nodiscard]] std::size_t nodeOf(std::size_t cell) const {
        return t_.land->land(cell) ? t_.land->slot(cell) : landNodes_ + t_.straits->slot(cell);
    }

    [[nodiscard]] bool claimed(std::size_t node) const {
        return node < landNodes_ ? out_.owner[node] != 0 : out_.passage[node - landNodes_] != 0;
    }

    void claim(std::size_t node, std::uint16_t s) {
        if (node < landNodes_) {
            out_.owner[node] = static_cast<std::uint16_t>(s + 1);
            ++out_.cells[s];
        } else {
            out_.passage[node - landNodes_] = static_cast<std::uint16_t>(s + 1);
        }
    }

    // Queues (cell, s) at cost, unless s already waits there for less (or as much, and not
    // across a barrier when this one is not).
    void push(std::size_t cell, std::size_t node, std::uint16_t s, std::uint32_t cost,
              bool barrier) {
        const bool same = (tag_[node] & ~kBarrierTag) == s;
        const bool better = cost < best_[node];
        const bool lighter = same && (tag_[node] & kBarrierTag) != 0 && !barrier;
        if (same && !better && !lighter) {
            return;
        }
        if (better || lighter) {
            best_[node] = std::min(best_[node], cost);
            tag_[node] = static_cast<std::uint16_t>(s | (barrier ? kBarrierTag : 0));
        }
        const std::uint64_t hash = noise::deriveSeed(cell, s) >> 32;
        heap_.push({(static_cast<std::uint64_t>(cost) << 32) | hash,
                    static_cast<std::uint32_t>(cell), s, barrier});
        ++out_.pushes;
    }

    void grow(std::size_t cell, std::uint16_t s, std::uint32_t cost) {
        const int people = states_[s].people;
        const bool minor = states_[s].rank == StateRank::Minor;
        const double limit = rule_.maxCost * kScale;
        for (const PlanetNeighbor& n : kPlanetNeighbors) {
            Step step;
            if (!stepTo(cell, n.dx, n.dz, people, minor, step) || claimed(step.node)) {
                continue;
            }
            const double total = cost + std::max(1.0, std::floor(step.cost * kScale + 0.5));
            if (total <= limit) {
                push(step.cell, step.node, s, static_cast<std::uint32_t>(total), step.barrier);
            }
        }
    }

    [[nodiscard]] std::size_t at(std::size_t cell, int dx, int dz) const {
        const PlanetGrid& g = t_.grid;
        return g.index(g.wrapColumn(g.column(cell) + dx), g.wrapRow(g.row(cell) + dz));
    }

    // Whether the edge from cell a to its side neighbor (dx, dz) is a ridge.
    [[nodiscard]] bool ridge(std::size_t a, int dx, int dz) const {
        const std::size_t holder = dx < 0 || dz < 0 ? at(a, dx, dz) : a; // its bit is there
        if (!t_.land->land(holder)) {
            return false;
        }
        const std::uint8_t bit = dx != 0 ? kRidgeEastBit : kRidgeSouthBit;
        return (t_.mask[t_.land->slot(holder)] & bit) != 0;
    }

    // A land cell a diagonal step may pass by: dry and not under a lake.
    [[nodiscard]] bool open(std::size_t cell) const {
        return t_.land->land(cell) &&
               (t_.mask[t_.land->slot(cell)] & (kRiverBits | kLakeBit)) == 0;
    }

    // A diagonal step goes between two open cells, across no ridge: never across a river.
    [[nodiscard]] bool diagonalOpen(std::size_t from, int dx, int dz) const {
        const std::size_t a = at(from, dx, 0);
        const std::size_t b = at(from, 0, dz);
        return t_.land->land(from) && open(a) && open(b) && !ridge(from, dx, 0) &&
               !ridge(a, 0, dz) && !ridge(from, 0, dz) && !ridge(b, dx, 0);
    }

    // The crossing of the river of a land slot of that class: less its probable ford, or less
    // the bonus of a confluence, never below 0.
    [[nodiscard]] double crossing(std::size_t slot, int river) const {
        const double full = rule_.riverCrossing[static_cast<std::size_t>(river - 1)];
        const double bonus = (t_.mask[slot] & kConfluenceBit) != 0
                                 ? rule_.confluenceBonus
                                 : rule_.fordBonus * unquantize(t_.ford[slot]) * full;
        return std::max(0.0, full - bonus);
    }

    // The factor of a minor state on a land slot: cheaper on the floor of a valley, dearer up the
    // slopes (ExpansionRule::valleys).
    [[nodiscard]] double valleyFactor(std::size_t slot) const {
        const ValleyGrowth& v = rule_.valleys;
        const double above = t_.drainage[slot] * kDrainageStepM;
        const double rise = std::min(1.0, above / v.slopeHeightM);
        return 1.0 - v.valleyBonus + (v.valleyBonus + v.slopePenalty) * rise;
    }

    bool stepTo(std::size_t from, int dx, int dz, int people, bool minor, Step& step) const;

    const ExpansionTerrain& t_;
    const SuitabilityMaps& s_;
    const ExpansionRule& rule_;
    const std::vector<StatePlan>& states_;
    std::size_t landNodes_;
    std::vector<std::uint32_t> best_;
    std::vector<std::uint16_t> tag_; // the state of best_, and kBarrierTag
    std::priority_queue<Entry, std::vector<Entry>, Later> heap_;
    ExpansionResult out_;
};

bool Expansion::stepTo(std::size_t from, int dx, int dz, int people, bool minor,
                       Step& step) const {
    const bool diagonal = dx != 0 && dz != 0;
    step.cell = at(from, dx, dz);
    if (!t_.land->land(step.cell)) {
        if (diagonal || !t_.straits->land(step.cell)) {
            return false;
        }
        step.node = landNodes_ + t_.straits->slot(step.cell);
        step.cost = rule_.seaCost;
        step.barrier = true;
        return true;
    }
    const std::size_t to = t_.land->slot(step.cell);
    const double suitability =
        unquantize(s_.byPeople[static_cast<std::size_t>(people)].values()[to]);
    if (suitability < rule_.minSuitability || (diagonal && !diagonalOpen(from, dx, dz))) {
        return false;
    }
    const bool fromLand = t_.land->land(from);
    const std::uint8_t was = fromLand ? t_.mask[t_.land->slot(from)] : 0;
    const std::uint8_t is = t_.mask[to];
    const double valley = minor ? valleyFactor(to) : 1.0;
    double cost = std::max(rule_.minCost,
                           t_.cost[to] * valley * (1.0 - rule_.preferenceWeight * suitability)) *
                  (diagonal ? kDiagonal : 1.0);
    // Half of the crossing of a river on entering it from a bank, half on leaving it to a bank:
    // a state on the river pays again to spill over the other bank. A state filling up to its
    // barriers may take the rivers and the lakes along its land, but neither leave them for the
    // land beyond nor follow them away: the water along it stays in it, the border runs along it.
    bool barrier = false;
    const int into = is & kRiverBits;
    const int outOf = was & kRiverBits;
    if (into > 0 && outOf == 0) {
        cost += 0.5 * crossing(to, into);
    } else if (outOf > 0 && into == 0) {
        cost += 0.5 * crossing(t_.land->slot(from), outOf);
        barrier = outOf >= rule_.barrierRiverClass;
    } else if (outOf > 0) {
        barrier = true; // following the river
    }
    if ((is & kLakeBit) != 0 && (was & kLakeBit) == 0) {
        cost += rule_.lakeCost;
    }
    barrier = barrier || (was & kLakeBit) != 0; // leaving or crossing a lake
    if (!diagonal && fromLand && ridge(from, dx, dz)) {
        cost += rule_.ridgeCost;
        barrier = true;
    }
    // Up the valleys of a massif to its crest (a ridge stops it), not onto its hostile summits.
    const bool fromHostile = fromLand && t_.hostile[t_.land->slot(from)] != 0;
    barrier = barrier || ((is & kMountainBit) != 0 && t_.hostile[to] != 0 && !fromHostile);
    step.node = to;
    step.cost = cost;
    step.barrier = barrier;
    return true;
}

} // namespace

ExpansionResult expand(const ExpansionTerrain& terrain, const SuitabilityMaps& suitability,
                       const ExpansionRule& rule, const std::vector<StatePlan>& states,
                       const std::vector<std::size_t>& capitals, double landCap) {
    const auto start = std::chrono::steady_clock::now();
    ExpansionResult r = Expansion(terrain, suitability, rule, states).run(capitals, landCap);
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return r;
}

} // namespace terrain::settlement
