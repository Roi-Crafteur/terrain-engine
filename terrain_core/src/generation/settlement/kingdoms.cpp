#include "terrain/generation/settlement/kingdoms.hpp"

#include "settlement_hash.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/settlement/geology.hpp"

#include <algorithm>
#include <chrono>
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
    k.timings.analysis = since(start);
    return k;
}

} // namespace terrain::settlement
