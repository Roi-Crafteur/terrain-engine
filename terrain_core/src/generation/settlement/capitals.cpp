#include "terrain/generation/settlement/capitals.hpp"

#include "../planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/settlement/geology.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr int kBucket = 64; // cells per side of a bucket of the capitals placed

// The smallest byte value of the habitability such that the land cells at or above it are at
// most share of the land (256: none).
int thresholdOf(const SettlementMaps& m, double share) {
    std::array<std::size_t, 256> histogram{};
    for (const std::uint8_t h : m.habitability.values()) {
        ++histogram[h];
    }
    const double limit = share * static_cast<double>(m.land->landCells());
    std::size_t above = 0;
    int t = 256;
    while (t > 0 && static_cast<double>(above + histogram[static_cast<std::size_t>(t - 1)]) <= limit) {
        above += histogram[static_cast<std::size_t>(--t)];
    }
    return t;
}

bool underLake(const SettlementMaps& m, std::size_t slot) {
    return (m.water.values()[slot] & (kFreshLakeBit | kSaltLakeBit)) != 0;
}

// The buckets of the interval [a, b] of a period of n cells, in buckets of kBucket.
std::vector<int> bucketsOf(int a, int b, int n) {
    std::vector<int> out;
    const int buckets = (n + kBucket - 1) / kBucket;
    if (b - a + 1 >= n) {
        for (int k = 0; k < buckets; ++k) {
            out.push_back(k);
        }
        return out;
    }
    const int wa = ((a % n) + n) % n;
    const int wb = ((b % n) + n) % n;
    const auto span = [&](int from, int to) {
        for (int k = from / kBucket; k <= to / kBucket; ++k) {
            out.push_back(k);
        }
    };
    if (wa <= wb) {
        span(wa, wb);
    } else {
        span(wa, n - 1);
        span(0, wb);
    }
    return out;
}

enum class Verdict : std::uint8_t { Clear, Blocked, Dead };

// The capitals placed so far, for the spacing.
class CapitalIndex {
public:
    explicit CapitalIndex(const PlanetGrid& grid)
        : grid_(grid), bx_((grid.nx + kBucket - 1) / kBucket),
          buckets_(static_cast<std::size_t>(bx_ * ((grid.nz + kBucket - 1) / kBucket))) {}

    void add(std::size_t cell, double radius) {
        const int i = grid_.column(cell);
        const int j = grid_.row(cell);
        buckets_[static_cast<std::size_t>((j / kBucket) * bx_ + i / kBucket)].push_back(
            static_cast<int>(radii_.size()));
        x_.push_back(grid_.centerX(i));
        z_.push_back(grid_.centerZ(j));
        radii_.push_back(radius);
        maxRadius_ = std::max(maxRadius_, radius);
    }

    // Clear if a capital of radius r at cell is at least factor (r + r_o) from every capital o;
    // Dead if it is closer than factor (rMin + r_o) to one (it stays refused for every radius of
    // at least rMin at this factor), else Blocked.
    [[nodiscard]] Verdict check(std::size_t cell, double r, double rMin, double factor) const {
        const int i = grid_.column(cell);
        const int j = grid_.row(cell);
        const double x = grid_.centerX(i);
        const double z = grid_.centerZ(j);
        const int reach = static_cast<int>(std::ceil(factor * (r + maxRadius_) / grid_.cellSize));
        bool blocked = false;
        for (const int bj : bucketsOf(j - reach, j + reach, grid_.nz)) {
            for (const int bi : bucketsOf(i - reach, i + reach, grid_.nx)) {
                for (const int o : buckets_[static_cast<std::size_t>(bj * bx_ + bi)]) {
                    const auto k = static_cast<std::size_t>(o);
                    const double d =
                        torusDistance(x, z, x_[k], z_[k], grid_.periodX(), grid_.periodZ());
                    if (d < factor * (rMin + radii_[k])) {
                        return Verdict::Dead;
                    }
                    blocked = blocked || d < factor * (r + radii_[k]);
                }
            }
        }
        return blocked ? Verdict::Blocked : Verdict::Clear;
    }

private:
    const PlanetGrid& grid_;
    int bx_;
    std::vector<std::vector<int>> buckets_;
    std::vector<double> x_;
    std::vector<double> z_;
    std::vector<double> radii_;
    double maxRadius_ = 0.0;
};

// Cells best first (suitability down, then cell up), each with the lowest relaxation at which it
// may still be taken.
struct Ranked {
    std::vector<std::uint32_t> cells;
    std::vector<std::uint8_t> minLevel;
    bool built = false;
};

void rank(Ranked& r, std::vector<std::uint64_t>& keys) {
    std::sort(keys.begin(), keys.end());
    for (const std::uint64_t k : keys) {
        r.cells.push_back(static_cast<std::uint32_t>(k & 0xFFFFFFFFu));
    }
    r.minLevel.assign(r.cells.size(), 0);
    r.built = true;
}

std::uint64_t keyOf(std::uint8_t suitability, std::size_t cell) {
    return (static_cast<std::uint64_t>(255 - suitability) << 32) | cell;
}

// What the placement reads.
struct Inputs {
    const SettlementMaps& m;
    const SuitabilityMaps& s;
    const CapitalRule& rule;
    std::vector<std::uint8_t> candidate; // by slot
};

// The seeds of a people: the candidates where its suitability is above 0 and a local maximum
// among the candidates within rule.seedRadius cells.
Ranked seedsOf(const Inputs& in, int people) {
    const PlanetGrid& g = in.m.grid;
    const auto& v = in.s.byPeople[static_cast<std::size_t>(people)].values();
    const int radius = in.rule.seedRadius;
    std::vector<std::uint64_t> keys;
    in.m.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        if (in.candidate[slot] == 0 || v[slot] == 0) {
            return;
        }
        const int i = g.column(cell);
        const int j = g.row(cell);
        for (int dj = -radius; dj <= radius; ++dj) {
            for (int di = -radius; di <= radius; ++di) {
                const std::size_t n = g.index(g.wrapColumn(i + di), g.wrapRow(j + dj));
                if (n == cell || !in.m.land->land(n)) {
                    continue;
                }
                const std::size_t ns = in.m.land->slot(n);
                if (in.candidate[ns] != 0 && (v[ns] > v[slot] || (v[ns] == v[slot] && n < cell))) {
                    return;
                }
            }
        }
        keys.push_back(keyOf(v[slot], cell));
    });
    Ranked r;
    rank(r, keys);
    return r;
}

// The candidates (onlyCandidates) or all the land cells not under a lake where the people can
// live.
Ranked cellsOf(const Inputs& in, int people, bool onlyCandidates) {
    const auto& v = in.s.byPeople[static_cast<std::size_t>(people)].values();
    std::vector<std::uint64_t> keys;
    in.m.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        const bool ok = onlyCandidates ? in.candidate[slot] != 0 : !underLake(in.m, slot);
        if (ok && v[slot] > 0) {
            keys.push_back(keyOf(v[slot], cell));
        }
    });
    Ranked r;
    rank(r, keys);
    return r;
}

// The placement, state by state.
class Placer {
public:
    Placer(const Inputs& in, const std::vector<StatePlan>& states, CapitalChoice& out)
        : in_(in), states_(states), out_(out), index_(in.m.grid),
          taken_(in.m.land->landCells(), 0), lists_(in.s.byPeople.size()) {}

    std::array<Ranked, 3>& lists(int people) { return lists_[static_cast<std::size_t>(people)]; }

    // Places the capital of state s; rMin is the smallest radius of the states of its rank.
    void place(std::size_t s, double rMin) {
        const StatePlan& state = states_[s];
        const double r = stateRadius(state.targetCells, in_.m.grid.cellSize);
        std::array<Ranked, 3>& ranked = lists(state.people);
        for (std::size_t kind = 0; kind < ranked.size(); ++kind) {
            if (!ranked[kind].built) {
                ranked[kind] = cellsOf(in_, state.people, kind == 1);
            }
            for (int level = 0; level <= in_.rule.relaxSteps; ++level) {
                const double factor = in_.rule.spacing * dmath::powInteger(in_.rule.relax, level);
                if (const std::size_t cell = scan(ranked[kind], level, r, rMin, factor);
                    cell != kNone) {
                    settle(s, cell, r, kind == 0, level);
                    return;
                }
            }
        }
        // Nothing far enough: the best cell not taken, whatever the spacing; at worst any land
        // cell not taken.
        for (const std::uint32_t cell : ranked[2].cells) {
            if (taken_[in_.m.land->slot(cell)] == 0) {
                settle(s, cell, r, false, in_.rule.relaxSteps + 1);
                return;
            }
        }
        bool placed = false;
        in_.m.land->forEachLand([&](std::size_t cell, std::size_t slot) {
            if (!placed && taken_[slot] == 0) {
                settle(s, cell, r, false, in_.rule.relaxSteps + 1);
                placed = true;
            }
        });
    }

    // Forgets which cells were refused for good: the next rank has other radii.
    void newRank() {
        for (auto& ranked : lists_) {
            for (Ranked& r : ranked) {
                std::fill(r.minLevel.begin(), r.minLevel.end(), std::uint8_t{0});
            }
        }
    }

private:
    static constexpr std::size_t kNone = ~std::size_t{0};

    std::size_t scan(Ranked& ranked, int level, double r, double rMin, double factor) {
        for (std::size_t k = 0; k < ranked.cells.size(); ++k) {
            const std::size_t cell = ranked.cells[k];
            if (ranked.minLevel[k] > level || taken_[in_.m.land->slot(cell)] != 0) {
                continue;
            }
            const Verdict v = index_.check(cell, r, rMin, factor);
            if (v == Verdict::Clear) {
                return cell;
            }
            if (v == Verdict::Dead) {
                ranked.minLevel[k] = static_cast<std::uint8_t>(level + 1);
            }
        }
        return kNone;
    }

    void settle(std::size_t s, std::size_t cell, double r, bool seed, int level) {
        const PlanetGrid& g = in_.m.grid;
        const std::size_t slot = in_.m.land->slot(cell);
        Capital& c = out_.capitals[s];
        c.cell = cell;
        c.x = g.centerX(g.column(cell));
        c.z = g.centerZ(g.row(cell));
        c.topHabitability = in_.m.habitability.values()[slot] >= out_.habitabilityThreshold;
        c.nearFreshWater = in_.m.waterKm.values()[slot] <= in_.rule.maxFreshWaterKm;
        c.seed = seed;
        c.relaxations = level;
        taken_[slot] = 1;
        index_.add(cell, r);
    }

    const Inputs& in_;
    const std::vector<StatePlan>& states_;
    CapitalChoice& out_;
    CapitalIndex index_;
    std::vector<std::uint8_t> taken_; // by slot
    std::vector<std::array<Ranked, 3>> lists_; // by people: seeds, candidates, land
};

// The states of a rank in the order they are served: one state of each people in turn, the
// peoples in the order of the states (by id).
std::vector<std::size_t> turnOrder(const std::vector<StatePlan>& states, StateRank rank) {
    std::vector<std::vector<std::size_t>> byPeople;
    std::vector<int> peoples;
    for (std::size_t s = 0; s < states.size(); ++s) {
        if (states[s].rank != rank) {
            continue;
        }
        if (peoples.empty() || peoples.back() != states[s].people) {
            peoples.push_back(states[s].people);
            byPeople.emplace_back();
        }
        byPeople.back().push_back(s);
    }
    std::vector<std::size_t> order;
    for (std::size_t round = 0;; ++round) {
        bool any = false;
        for (const auto& list : byPeople) {
            if (round < list.size()) {
                order.push_back(list[round]);
                any = true;
            }
        }
        if (!any) {
            break;
        }
    }
    return order;
}

} // namespace

double stateRadius(double cells, double cellSize) {
    return std::sqrt(cells / std::numbers::pi) * cellSize;
}

CapitalChoice placeCapitals(const SettlementMaps& maps, const SuitabilityMaps& suitability,
                            const CapitalRule& rule, const std::vector<StatePlan>& states,
                            int threads) {
    const auto start = std::chrono::steady_clock::now();
    CapitalChoice out;
    out.capitals.resize(states.size());
    const int threshold = thresholdOf(maps, rule.topHabitability);
    out.habitabilityThreshold = static_cast<std::uint8_t>(std::min(threshold, 255));
    Inputs in{maps, suitability, rule, std::vector<std::uint8_t>(maps.land->landCells(), 0)};
    maps.land->forEachLand([&](std::size_t, std::size_t slot) {
        const bool top = maps.habitability.values()[slot] >= threshold;
        const bool water = maps.waterKm.values()[slot] <= rule.maxFreshWaterKm;
        in.candidate[slot] = top && water && !underLake(maps, slot) ? 1 : 0;
        out.candidates += in.candidate[slot];
    });
    Placer placer(in, states, out);
    std::vector<int> peoples;
    for (const StatePlan& s : states) {
        if (std::find(peoples.begin(), peoples.end(), s.people) == peoples.end()) {
            peoples.push_back(s.people);
        }
    }
    planet::parallelBlocks(static_cast<int>(peoples.size()), 1, planet::resolveThreads(threads),
                           [&](int begin, int end) {
                               for (int k = begin; k < end; ++k) {
                                   const int p = peoples[static_cast<std::size_t>(k)];
                                   placer.lists(p)[0] = seedsOf(in, p);
                               }
                           });
    for (const int p : peoples) {
        out.seeds += placer.lists(p)[0].cells.size();
    }
    for (const StateRank rank : {StateRank::Major, StateRank::Minor}) {
        double rMin = 0.0;
        bool first = true;
        for (const StatePlan& s : states) {
            if (s.rank == rank) {
                const double r = stateRadius(s.targetCells, maps.grid.cellSize);
                rMin = first ? r : std::min(rMin, r);
                first = false;
            }
        }
        placer.newRank();
        for (const std::size_t s : turnOrder(states, rank)) {
            placer.place(s, rMin);
        }
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

} // namespace terrain::settlement
