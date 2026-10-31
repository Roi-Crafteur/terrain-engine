#include "terrain/generation/settlement/kingdoms.hpp"

#include "settlement_hash.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/settlement/geology.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

namespace terrain::settlement {

namespace {

using Clock = std::chrono::steady_clock;

double since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// The land each state holds.
std::vector<std::size_t> cellsOf(const std::vector<std::uint16_t>& owner, std::size_t states) {
    std::vector<std::size_t> cells(states, 0);
    for (const std::uint16_t o : owner) {
        if (o != 0) {
            ++cells[o - 1u];
        }
    }
    return cells;
}

// The share of the land held that each people holds.
std::vector<double> peopleShares(const std::vector<StatePlan>& states,
                                 const std::vector<std::size_t>& cells, std::size_t peoples) {
    std::vector<double> share(peoples, 0.0);
    double held = 0.0;
    for (std::size_t s = 0; s < states.size(); ++s) {
        share[static_cast<std::size_t>(states[s].people)] += static_cast<double>(cells[s]);
        held += static_cast<double>(cells[s]);
    }
    for (double& v : share) {
        v = held > 0.0 ? v / held : 0.0;
    }
    return share;
}

// Lowers the targets of the states of every people above maxShare of the land held; false if
// none is.
bool lowerTargets(std::vector<StatePlan>& states, const std::vector<std::size_t>& cells,
                  std::size_t peoples, double maxShare) {
    const std::vector<double> share = peopleShares(states, cells, peoples);
    bool over = false;
    for (StatePlan& s : states) {
        const double v = share[static_cast<std::size_t>(s.people)];
        if (v > maxShare) {
            s.targetCells = std::max(1.0, s.targetCells * 0.95 * maxShare / v);
            over = true;
        }
    }
    return over;
}

// The second moments of the cells of a state about its capital (on the torus), in cells.
struct Moments {
    double n = 0.0;
    double x = 0.0;
    double z = 0.0;
    double xx = 0.0;
    double zz = 0.0;
    double xz = 0.0;

    // The square root of the ratio of the principal moments about the centroid.
    [[nodiscard]] double elongation() const {
        const double a = xx / n - (x / n) * (x / n);
        const double c = zz / n - (z / n) * (z / n);
        const double b = xz / n - (x / n) * (z / n);
        const double mean = 0.5 * (a + c);
        const double spread = std::sqrt(0.25 * (a - c) * (a - c) + b * b);
        return mean - spread > 0.0 ? std::sqrt((mean + spread) / (mean - spread)) : 1.0;
    }
};

// The mean elongation of the states of each rank (KingdomStats::majorElongation).
void elongation(const ExpansionTerrain& t, const std::vector<StatePlan>& states,
                const std::vector<std::size_t>& capitals, const std::vector<std::uint16_t>& owner,
                KingdomStats& st) {
    std::vector<Moments> m(states.size());
    const PlanetGrid& g = t.grid;
    const auto wrap = [](int d, int n) { return d > n / 2 ? d - n : (d < -n / 2 ? d + n : d); };
    t.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        if (owner[slot] == 0) {
            return;
        }
        const std::size_t s = owner[slot] - 1u;
        const double dx = wrap(g.column(cell) - g.column(capitals[s]), g.nx);
        const double dz = wrap(g.row(cell) - g.row(capitals[s]), g.nz);
        Moments& k = m[s];
        k.n += 1.0;
        k.x += dx;
        k.z += dz;
        k.xx += dx * dx;
        k.zz += dz * dz;
        k.xz += dx * dz;
    });
    double sum[2] = {0.0, 0.0};
    double count[2] = {0.0, 0.0};
    for (std::size_t s = 0; s < states.size(); ++s) {
        if (m[s].n >= 3.0) {
            const std::size_t rank = states[s].rank == StateRank::Major ? 0 : 1;
            sum[rank] += m[s].elongation();
            count[rank] += 1.0;
        }
    }
    st.majorElongation = count[0] > 0.0 ? sum[0] / count[0] : 0.0;
    st.minorElongation = count[1] > 0.0 ? sum[1] / count[1] : 0.0;
}

} // namespace

KingdomStats kingdomStats(const ExpansionTerrain& terrain,
                          const SettlementMaps& maps, const SuitabilityMaps& suitability,
                          const SettlementData& data, const std::vector<StatePlan>& states,
                          const std::vector<std::uint16_t>& owner,
                          const std::vector<ClimateCut>& cuts) {
    KingdomStats st;
    const std::vector<std::size_t> cells = cellsOf(owner, states.size());
    const auto land = static_cast<double>(maps.land->landCells());
    double held = 0.0;
    std::vector<double> byPeople(data.peoples.size(), 0.0);
    for (std::size_t s = 0; s < states.size(); ++s) {
        held += static_cast<double>(cells[s]);
        byPeople[static_cast<std::size_t>(states[s].people)] += static_cast<double>(cells[s]);
        st.largestState = std::max(st.largestState, static_cast<double>(cells[s]) / land);
    }
    st.heldShare = held / land;
    for (std::size_t p = 0; p < byPeople.size(); ++p) {
        const People& people = data.peoples[p];
        const bool shares = people.majorKingdomShare > 0.0 || people.minorStateShare > 0.0;
        st.peoplesWithoutState += shares && byPeople[p] == 0.0 ? 1 : 0;
        if (held > 0.0 && byPeople[p] / held > st.largestPeople) {
            st.largestPeople = byPeople[p] / held;
            st.largestPeopleIndex = static_cast<int>(p);
        }
    }
    double heldHostile = 0.0;
    double unliked = 0.0;
    for (std::size_t slot = 0; slot < owner.size(); ++slot) {
        if (terrain.hostile[slot] == 0) {
            continue;
        }
        ++st.hostileCells;
        if (owner[slot] != 0) {
            const auto people = static_cast<std::size_t>(states[owner[slot] - 1u].people);
            heldHostile += 1.0;
            unliked += unquantize(suitability.byPeople[people].values()[slot]) <
                               data.config.goodWorld.likedSuitability
                           ? 1.0
                           : 0.0;
        }
    }
    const double n = std::max<double>(1.0, static_cast<double>(st.hostileCells));
    st.hostileHeld = heldHostile / n;
    st.hostileUnliked = unliked / n;
    st.borders = borderStats(terrain, owner, cuts, data.config.borders);
    st.massifs = massifStats(terrain, owner, data.config.borders);
    return st;
}

std::size_t Kingdoms::memoryBytes() const {
    return owner.capacity() * sizeof(std::uint16_t) + passage.capacity() * sizeof(std::uint16_t) +
           cells.capacity() * sizeof(std::size_t) + states.capacity() * sizeof(StatePlan) +
           capitals.capitals.capacity() * sizeof(Capital);
}

std::uint64_t Kingdoms::checksum() const {
    Fnv64 f;
    for (const StateCount& c : counts) {
        f.add(static_cast<std::uint64_t>(c.major), 4);
        f.add(static_cast<std::uint64_t>(c.minor), 4);
    }
    for (std::size_t s = 0; s < states.size(); ++s) {
        f.add(states[s].targetCells);
        f.add(static_cast<std::uint64_t>(capitals.capitals[s].cell));
    }
    for (const auto* list : {&owner, &passage}) {
        for (const std::uint16_t o : *list) {
            f.add(o, 2);
        }
    }
    for (const Ruler& r : politics.rulers) {
        for (const std::string* text : {&r.kingdom, &r.government, &r.name}) {
            f.addBytes({reinterpret_cast<const std::uint8_t*>(text->data()), text->size()});
        }
        for (const double t : r.personality) {
            f.add(t);
        }
        for (const std::string& goal : r.goals) {
            f.addBytes({reinterpret_cast<const std::uint8_t*>(goal.data()), goal.size()});
        }
    }
    for (const StateRelation& r : politics.relations) {
        f.add(static_cast<std::uint64_t>(r.from * 65536 + r.to));
        f.add(r.relation);
        f.add(r.tension);
    }
    return f.value();
}

Kingdoms buildKingdoms(const PlanetLevel& level, const SettlementMaps& maps,
                       const SuitabilityMaps& suitability, const ExpansionTerrain& terrain,
                       const SettlementData& data, const SettlementRequest& request,
                       std::uint64_t worldSeed, int threads) {
    const SettlementConfig& c = data.config;
    Kingdoms k;
    k.preset = resolvePreset(c, request, worldSeed, worldScale(maps.grid, c));
    k.counts = allocateStates(data.peoples, k.preset.majorKingdoms, k.preset.minorStates);
    k.states = planStates(c, k.preset, data.peoples, k.counts, maps.land->landCells(), worldSeed);
    Clock::time_point start = Clock::now();
    k.capitals = placeCapitals(maps, suitability, c.capitals, k.states, threads,
                               {&terrain.compartments, c.expansion.barrierStop,
                                c.expansion.overflow});
    k.timings.capitals = since(start);
    std::vector<std::size_t> capitals;
    for (const Capital& capital : k.capitals.capitals) {
        capitals.push_back(capital.cell);
    }
    const double landCap = c.states.maxLandShare * static_cast<double>(maps.land->landCells());
    start = Clock::now();
    ExpansionResult e = expand(terrain, suitability, c.expansion, k.states, capitals, landCap);
    while (k.reruns < c.expansion.peopleReruns &&
           lowerTargets(k.states, e.cells, data.peoples.size(), c.expansion.maxPeopleShare)) {
        e = expand(terrain, suitability, c.expansion, k.states, capitals, landCap);
        ++k.reruns;
    }
    k.timings.expansion = since(start);
    k.pushes = e.pushes;
    start = Clock::now();
    k.owner = cleanOwners(terrain, e.owner, capitals, c.borders);
    k.passage = std::move(e.passage);
    k.cells = cellsOf(k.owner, k.states.size());
    k.timings.cleanup = since(start);
    start = Clock::now();
    k.cuts = climateCuts(level, *maps.land, c.borders);
    k.stats = kingdomStats(terrain, maps, suitability, data, k.states, k.owner, k.cuts);
    for (const Capital& capital : k.capitals.capitals) {
        k.stats.capitalFails += capital.topHabitability && capital.nearFreshWater ? 0 : 1;
    }
    elongation(terrain, k.states, capitals, k.owner, k.stats);
    k.politics = buildPolitics(terrain, maps, suitability, data, k.states, k.owner, worldSeed);
    k.timings.analysis = since(start);
    return k;
}

} // namespace terrain::settlement
