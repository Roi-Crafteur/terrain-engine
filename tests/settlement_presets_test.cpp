// The world presets, the states and their capitals (generation/settlement/world_preset.hpp,
// capitals.hpp): the values drawn in the ranges of the preset from the seed (the same seed, the
// same values), the forced values, the validated rounding of the shares of the peoples (the
// example of the document, at least one state in all, never the only state taken, ties by id),
// the target areas, and the capitals of the default world in cells of 65,536 m: in the most
// habitable fifth of the land and near fresh water, far enough apart, the same whatever threads,
// with or without the compartments.

#include "settlement_world_support.hpp"
#include "terrain/generation/settlement/capitals.hpp"
#include "terrain/generation/settlement/geology.hpp"
#include "terrain/generation/settlement/world_preset.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace st = terrain::settlement;

namespace {

st::People people(const std::string& id, double major, double minor) {
    st::People p;
    p.id = id;
    p.majorKingdomShare = major;
    p.minorStateShare = minor;
    return p;
}

int total(const std::vector<st::StateCount>& counts, bool major) {
    int sum = 0;
    for (const st::StateCount& c : counts) {
        sum += major ? c.major : c.minor;
    }
    return sum;
}

void testResolve(const st::SettlementConfig& config) {
    const st::ResolvedPreset wild = st::resolvePreset(config, {}, 42, 1.0);
    TEST_CHECK(wild.preset == "wild");
    TEST_CHECK(wild.civilizedShare >= 0.10 && wild.civilizedShare <= 0.20);
    TEST_CHECK(wild.majorKingdoms >= 10 && wild.majorKingdoms <= 20);
    TEST_CHECK(wild.minorStates >= 50 && wild.minorStates <= 150);
    // The same seed, the same values; the draws cover their range.
    const st::ResolvedPreset again = st::resolvePreset(config, {}, 42, 1.0);
    TEST_CHECK(again.majorKingdoms == wild.majorKingdoms && again.minorStates == wild.minorStates &&
               again.civilizedShare == wild.civilizedShare);
    std::set<int> majors;
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        const st::ResolvedPreset c =
            st::resolvePreset(config, {"civilized", {}, {}, {}}, seed, 1.0);
        TEST_CHECK(c.majorKingdoms >= 80 && c.majorKingdoms <= 150);
        TEST_CHECK(c.minorStates >= 500 && c.minorStates <= 1000);
        TEST_CHECK(c.civilizedShare >= 0.5 && c.civilizedShare <= 0.7);
        majors.insert(c.majorKingdoms);
    }
    TEST_CHECK(majors.size() > 20);
    // A world of half the area of the reference planet draws half the counts.
    const st::ResolvedPreset half = st::resolvePreset(config, {"balanced", {}, {}, {}}, 42, 0.5);
    TEST_CHECK(half.majorKingdoms >= 15 && half.majorKingdoms <= 30);
    // Forced values, and an exact preset, are taken as they are, whatever the scale.
    const st::ResolvedPreset forced = st::resolvePreset(config, {"wild", 0.3, 12, 85}, 42, 0.01);
    TEST_CHECK(forced.majorKingdoms == 12 && forced.minorStates == 85 &&
               forced.civilizedShare == 0.3);
    st::SettlementConfig exact = config;
    exact.presets[0].majorKingdoms = {true, 12.0, 12.0};
    TEST_CHECK(st::resolvePreset(exact, {}, 7, 0.01).majorKingdoms == 12);
    std::string message;
    try {
        static_cast<void>(st::resolvePreset(config, {"medieval", {}, {}, {}}, 42, 1.0));
    } catch (const std::invalid_argument& e) {
        message = e.what();
    }
    TEST_CHECK(message == "Invalid preset 'medieval' (expected one of: wild, balanced, civilized)");
}

void testAllocation() {
    // The example of the document: 40 major kingdoms, the humans at 0.35 of the shares: 14.
    std::vector<st::People> peoples = {people("humains", 35, 0), people("elfes", 10, 0),
                                       people("nains", 8, 0),    people("orcs", 8, 0),
                                       people("gobelins", 7, 0), people("autres", 32, 0)};
    std::vector<st::StateCount> c = st::allocateStates(peoples, 40, 0);
    TEST_CHECK(c[0].major == 14 && total(c, true) == 40);
    // Shares need not add up to 1, nor to 100: the same counts at any scale.
    for (st::People& p : peoples) {
        p.majorKingdomShare /= 100.0;
    }
    const std::vector<st::StateCount> scaled = st::allocateStates(peoples, 40, 0);
    for (std::size_t k = 0; k < c.size(); ++k) {
        TEST_CHECK(scaled[k].major == c[k].major);
    }
    // At least one state in all: no major share, a minor one -> a minor state; a tiny major share
    // alone -> a major kingdom; no share -> nothing. The totals are kept where they can be.
    peoples = {people("a", 10, 10), people("b", 0, 0.01), people("c", 0.01, 0), people("d", 0, 0)};
    c = st::allocateStates(peoples, 5, 5);
    TEST_CHECK(c[1].major == 0 && c[1].minor == 1);
    TEST_CHECK(c[2].major == 1 && c[2].minor == 0);
    TEST_CHECK(c[3].major == 0 && c[3].minor == 0);
    TEST_CHECK(total(c, true) == 5 && total(c, false) == 5 && c[0].major == 4 && c[0].minor == 4);
    // Never the only state of a people: 13 peoples and 5 minor states give 13.
    peoples.clear();
    for (char k = 'a'; k < 'a' + 13; ++k) {
        peoples.push_back(people(std::string(1, k), 0, 1));
    }
    c = st::allocateStates(peoples, 0, 5);
    TEST_CHECK(total(c, false) == 13 && total(c, true) == 0);
    // Ties go to the smaller id, whatever the order of the files.
    peoples = {people("orcs", 1, 0), people("elfes", 1, 0), people("nains", 1, 0)};
    c = st::allocateStates(peoples, 4, 0);
    TEST_CHECK(c[1].major == 2 && c[0].major == 1 && c[2].major == 1);
    peoples = {people("orcs", 1, 0), people("elfes", 1, 0), people("nains", 1, 0)};
    c = st::allocateStates(peoples, 5, 0);
    TEST_CHECK(c[1].major == 2 && c[2].major == 2 && c[0].major == 1);
}

void testPlans(const st::SettlementData& data) {
    const st::SettlementConfig& config = data.config;
    const st::ResolvedPreset preset{"wild", 0.15, 14, 90};
    const std::vector<st::StateCount> counts = st::allocateStates(data.peoples, 14, 90);
    TEST_CHECK(total(counts, true) == 14 && total(counts, false) == 90);
    const std::size_t land = 1'000'000;
    const std::vector<st::StatePlan> plans =
        st::planStates(config, preset, data.peoples, counts, land, 5);
    TEST_CHECK(plans.size() == 104);
    double major = 0.0;
    double all = 0.0;
    for (std::size_t s = 0; s < plans.size(); ++s) {
        TEST_CHECK((plans[s].rank == st::StateRank::Major) == (s < 14));
        if (s > 0 && plans[s].rank == plans[s - 1].rank) {
            TEST_CHECK(data.peoples[static_cast<std::size_t>(plans[s - 1].people)].id <=
                       data.peoples[static_cast<std::size_t>(plans[s].people)].id);
        }
        TEST_CHECK(plans[s].targetCells <= 0.15 * land);
        all += plans[s].targetCells;
        major += plans[s].rank == st::StateRank::Major ? plans[s].targetCells : 0.0;
    }
    std::printf("plans: %.0f cells in all, %.2f to the major kingdoms\n", all, major / all);
    TEST_CHECK(std::abs(all / (0.15 * land) - 1.0) < 0.1);
    TEST_CHECK(std::abs(major / all - 0.7) < 0.07);
    // One kingdom asked for more than 15 % of the land is held at 15 %.
    const std::vector<st::StatePlan> one = st::planStates(
        config, {"wild", 0.9, 1, 0}, {people("solo", 1, 0)}, {{1, 0}}, land, 5);
    TEST_CHECK(one.size() == 1 && one[0].targetCells == 0.15 * land);
}

void checkCapitals(const st::SettlementMaps& m, const st::CapitalChoice& choice,
                   const std::vector<st::StatePlan>& plans, const st::CapitalRule& rule) {
    std::size_t top = 0;
    for (const std::uint8_t h : m.habitability.values()) {
        top += h >= choice.habitabilityThreshold ? 1 : 0;
    }
    TEST_CHECK(static_cast<double>(top) <=
               rule.topHabitability * static_cast<double>(m.land->landCells()));
    std::set<std::size_t> cells;
    int fails = 0;
    int relaxed = 0;
    for (std::size_t s = 0; s < plans.size(); ++s) {
        const st::Capital& c = choice.capitals[s];
        TEST_CHECK(m.land->land(c.cell));
        cells.insert(c.cell);
        fails += c.topHabitability && c.nearFreshWater ? 0 : 1;
        relaxed += c.relaxations > 0 ? 1 : 0;
        // Spacing: two capitals of the same rank both placed without relaxing are at least
        // spacing times the sum of their radii apart; a minor state, placed after every major
        // kingdom, at least its relaxed factor from each.
        const double rs = st::stateRadius(plans[s].targetCells, m.grid.cellSize);
        for (std::size_t o = 0; o < plans.size(); ++o) {
            const st::Capital& d = choice.capitals[o];
            const bool same = o != s && plans[o].rank == plans[s].rank && c.relaxations == 0 &&
                              d.relaxations == 0;
            const bool after = plans[o].rank == st::StateRank::Major &&
                               plans[s].rank == st::StateRank::Minor &&
                               c.relaxations <= rule.relaxSteps;
            if (!same && !after) {
                continue;
            }
            const double factor = rule.spacing * std::pow(rule.relax, c.relaxations);
            const double ro = st::stateRadius(plans[o].targetCells, m.grid.cellSize);
            TEST_CHECK(st::torusDistance(c.x, c.z, d.x, d.z, m.grid.periodX(), m.grid.periodZ()) >=
                       factor * (rs + ro) * (1.0 - 1e-9));
        }
    }
    TEST_CHECK(cells.size() == plans.size());
    std::printf("  %zu capitals, %zu candidates, %zu seeds, threshold %d, %d relaxed, %d outside "
                "the most habitable fifth or far from fresh water, %.2f s\n",
                plans.size(), choice.candidates, choice.seeds, choice.habitabilityThreshold,
                relaxed, fails, choice.seconds);
    TEST_CHECK(fails == 0);
}

// With the compartments, the minor states placed in one take a free compartment of their size,
// one capital each, and keep the other rules.
void checkCompartments(const st::CapitalChoice& choice, const std::vector<st::StatePlan>& plans,
                       const st::SettlementMaps& m, const st::CompartmentFit& fit) {
    std::set<std::uint32_t> used;
    int fitted = 0;
    int minors = 0;
    for (std::size_t s = 0; s < plans.size(); ++s) {
        minors += plans[s].rank == st::StateRank::Minor ? 1 : 0;
        if (!choice.capitals[s].compartment) {
            continue;
        }
        ++fitted;
        const std::uint32_t id = fit.compartments->of(m.land->slot(choice.capitals[s].cell));
        const double cells = fit.compartments->cells[id];
        TEST_CHECK(plans[s].rank == st::StateRank::Minor);
        TEST_CHECK(cells >= fit.low * plans[s].targetCells &&
                   cells <= fit.high * plans[s].targetCells);
        TEST_CHECK(used.insert(id).second);
    }
    std::printf("  %d of the %d minor states in a compartment of %.1f to %.1f times their target\n",
                fitted, minors, fit.low, fit.high);
}

void testCapitals(const settlement_test::World& w) {
    const st::SettlementMaps m =
        st::computeSettlementMaps(w.level, &w.field, w.data, settlement_test::kSeed, 0);
    const st::SuitabilityMaps s =
        st::computeSuitability(w.level, m, w.data, settlement_test::kSeed, 0);
    const st::ExpansionTerrain t =
        st::buildExpansionTerrain(w.level, m, w.data.config, settlement_test::kSeed, 0);
    // In cells of 65 km fresh water within 10 km is a river or a lake in the cell itself: the seeds
    // are never in a compartment here (settlement_expansion_test places one on a hand-made map);
    // the other rules must hold all the same.
    const st::CompartmentFit fit{&t.compartments, w.data.config.expansion.barrierStop,
                                 w.data.config.expansion.overflow};
    for (const char* id : {"wild", "civilized"}) {
        const st::ResolvedPreset preset =
            st::resolvePreset(w.data.config, {id, {}, {}, {}}, settlement_test::kSeed, 1.0);
        const std::vector<st::StatePlan> plans = st::planStates(
            w.data.config, preset, w.data.peoples,
            st::allocateStates(w.data.peoples, preset.majorKingdoms, preset.minorStates),
            m.land->landCells(), settlement_test::kSeed);
        std::printf("%s: %d major kingdoms, %d minor states, civilized share %.3f\n", id,
                    preset.majorKingdoms, preset.minorStates, preset.civilizedShare);
        const st::CapitalChoice choice = st::placeCapitals(m, s, w.data.config.capitals, plans, 0);
        checkCapitals(m, choice, plans, w.data.config.capitals);
        const st::CapitalChoice inside =
            st::placeCapitals(m, s, w.data.config.capitals, plans, 0, fit);
        checkCapitals(m, inside, plans, w.data.config.capitals);
        checkCompartments(inside, plans, m, fit);
        // The same capitals whatever the threads.
        const st::CapitalChoice one =
            st::placeCapitals(m, s, w.data.config.capitals, plans, 1, fit);
        bool same = one.capitals.size() == inside.capitals.size();
        for (std::size_t k = 0; same && k < one.capitals.size(); ++k) {
            same = one.capitals[k].cell == inside.capitals[k].cell;
        }
        TEST_CHECK(same);
    }
}

} // namespace

int main() {
    const settlement_test::World w;
    testResolve(w.data.config);
    testAllocation();
    testPlans(w.data);
    testCapitals(w);
    return test::result();
}
