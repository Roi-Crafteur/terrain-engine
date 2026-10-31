// The rulers and the relations of the states (generation/settlement/politics.hpp) on a hand-made
// map: three states in a row, A and C of the people alpha, B of the people beta; B holds the metal
// alpha needs. One ruler per state of a government of its people, its personality in the ranges of
// its people, its name the same for the same seed; the relations of neighbors only, asymmetric,
// selfRelation between two states of one people; the tension in [0, 1] from its terms; the goals
// from the situation. No two kingdoms of one name.

#include "terrain/generation/settlement/politics.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace st = terrain::settlement;
namespace planet = terrain::planet;

namespace {

// The land between the columns 2 and 25 of a grid of 30 x 10: A on 2 to 9, C on 10 to 17, B on
// 18 to 25.
struct Map {
    st::ExpansionTerrain terrain;
    st::SettlementMaps maps;
    st::SuitabilityMaps suitability;
    std::vector<std::uint16_t> owner;
};

Map makeMap(const st::SettlementData& data) {
    Map m;
    m.terrain.grid = terrain::PlanetGrid{30, 10, 8192.0};
    std::vector<std::uint32_t> cells;
    for (int j = 0; j < 10; ++j) {
        for (int i = 2; i < 26; ++i) {
            cells.push_back(static_cast<std::uint32_t>(m.terrain.grid.index(i, j)));
        }
    }
    std::sort(cells.begin(), cells.end());
    m.terrain.land = std::make_shared<const planet::LandIndex>(
        planet::LandIndex::ofCells(m.terrain.grid.cells(), cells));
    const auto land = m.terrain.land;
    m.maps.grid = m.terrain.grid;
    m.maps.land = land;
    m.maps.mana = planet::LandValues<std::uint8_t>(land, 0, 0);
    const auto plains = static_cast<std::uint8_t>(data.config.model("plains") -
                                                  data.config.terrainModels.data());
    m.maps.terrainModel = planet::LandValues<std::uint8_t>(land, plains, 0);
    m.maps.resources.assign(data.resources.size(), planet::LandValues<std::uint8_t>(land, 0, 0));
    m.suitability.byPeople.assign(2, planet::LandValues<std::uint8_t>(land, 255, 0));
    m.owner.assign(land->landCells(), 0);
    const auto metal =
        static_cast<std::size_t>(data.resource("metaux_communs") - data.resources.data());
    land->forEachLand([&](std::size_t cell, std::size_t slot) {
        const int i = m.terrain.grid.column(cell);
        m.owner[slot] = static_cast<std::uint16_t>(i < 10 ? 1 : (i < 18 ? 2 : 3));
        if (i >= 18) {
            m.maps.resources[metal].values()[slot] = 200;
        }
    });
    return m;
}

st::People people(const std::string& id, const std::string& other, double relation) {
    st::People p;
    p.id = id;
    p.government = {"crown", "council"};
    p.titles = {{"crown", "King"}, {"council", "Speaker"}};
    p.syllables = {"ka", "ro", "mi", "thu", "len"};
    p.relations = {{other, relation}};
    return p;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

void testRulers(const st::SettlementData& data, const Map& m,
                const std::vector<st::StatePlan>& states) {
    const st::Politics p =
        st::buildPolitics(m.terrain, m.maps, m.suitability, data, states, m.owner, 42);
    TEST_CHECK(p.rulers.size() == 3 && p.rulers[0].id == "ruler_000" &&
               p.rulers[2].id == "ruler_002");
    for (std::size_t s = 0; s < 3; ++s) {
        const st::Ruler& r = p.rulers[s];
        const st::People& who = data.peoples[static_cast<std::size_t>(states[s].people)];
        std::printf("%s of %s (%s), goals %zu\n", r.name.c_str(), r.kingdom.c_str(),
                    r.government.c_str(), r.goals.size());
        TEST_CHECK(r.government == "crown" || r.government == "council");
        TEST_CHECK(startsWith(r.name, who.titleOf(r.government) + " "));
        for (std::size_t k = 0; k < 5; ++k) {
            TEST_CHECK(r.personality[k] >= who.traits[k].min &&
                       r.personality[k] <= who.traits[k].max);
        }
        TEST_CHECK(!r.goals.empty() && r.goals.size() <= 3);
    }
    // A and C need the metal they lack, and A, very ambitious and aggressive, eyes its neighbor C.
    const auto& goals = p.rulers[0].goals;
    TEST_CHECK(std::find(goals.begin(), goals.end(), "secure_metaux_communs") != goals.end());
    TEST_CHECK(std::find(goals.begin(), goals.end(), "weaken_" + p.rulers[1].kingdom) !=
               goals.end());
    // The same seed, the same rulers; another seed, other names.
    const st::Politics again =
        st::buildPolitics(m.terrain, m.maps, m.suitability, data, states, m.owner, 42);
    const st::Politics other =
        st::buildPolitics(m.terrain, m.maps, m.suitability, data, states, m.owner, 43);
    TEST_CHECK(again.rulers[1].name == p.rulers[1].name &&
               again.rulers[1].goals == p.rulers[1].goals);
    bool renamed = false;
    for (std::size_t s = 0; s < 3; ++s) {
        renamed = renamed || other.rulers[s].name != p.rulers[s].name;
    }
    TEST_CHECK(renamed);
}

void testRelations(const st::SettlementData& data, const Map& m,
                   const std::vector<st::StatePlan>& states) {
    const st::Politics p =
        st::buildPolitics(m.terrain, m.maps, m.suitability, data, states, m.owner, 42);
    TEST_CHECK(p.relations.size() == 4); // A-C and C-B both ways; A and B do not touch
    const auto find = [&](std::size_t from, std::size_t to) {
        for (const st::StateRelation& r : p.relations) {
            if (r.from == from && r.to == to) {
                return r;
            }
        }
        return st::StateRelation{99, 99};
    };
    const st::StateRelation ac = find(0, 1);
    const st::StateRelation ca = find(1, 0);
    const st::StateRelation cb = find(1, 2);
    const st::StateRelation bc = find(2, 1);
    TEST_CHECK(ac.from == 0 && ca.from == 1 && cb.from == 1 && bc.from == 2);
    TEST_CHECK(ac.relation == 0.4 && ca.relation == 0.4); // selfRelation of alpha
    TEST_CHECK(cb.relation == -0.6 && bc.relation == 0.2); // asymmetric
    TEST_CHECK(ac.border == 1.0 && ca.border == 0.5 && cb.border == 0.5 && ac.borderEdges == 10);
    TEST_CHECK(cb.coveted > 0.7 && bc.coveted == 0.0);
    for (const st::StateRelation& r : p.relations) {
        TEST_CHECK(r.tension >= 0.0 && r.tension <= 1.0);
    }
    TEST_CHECK(cb.tension > bc.tension);
}

// Forty states of a people of two syllables: more states than names of two or three syllables
// (12), every kingdom still has its own name.
void testUniqueNames(st::SettlementData data, const Map& m) {
    data.peoples[0].syllables = {"ka", "ro"};
    const std::vector<st::StatePlan> states(40, st::StatePlan{0, st::StateRank::Minor, 80.0});
    const st::Politics p =
        st::buildPolitics(m.terrain, m.maps, m.suitability, data, states, m.owner, 42);
    std::vector<std::string> kingdoms;
    for (const st::Ruler& r : p.rulers) {
        kingdoms.push_back(r.kingdom);
    }
    std::sort(kingdoms.begin(), kingdoms.end());
    TEST_CHECK(kingdoms.size() == 40 &&
               std::adjacent_find(kingdoms.begin(), kingdoms.end()) == kingdoms.end());
    std::printf("kingdoms %s ... %s\n", kingdoms.front().c_str(), kingdoms.back().c_str());
}

} // namespace

int main() {
    st::SettlementData data = st::loadSettlementData(TERRAIN_SETTLEMENT_DATA);
    st::People alpha = people("alpha", "beta", -0.6);
    alpha.selfRelation = 0.4;
    alpha.needs = {{"metaux_communs", 1.0}};
    alpha.traits[0] = {0.8, 0.9}; // ambition
    alpha.traits[1] = {0.9, 1.0}; // aggression
    data.peoples = {alpha, people("beta", "alpha", 0.2)};
    const Map m = makeMap(data);
    const std::vector<st::StatePlan> states = {{0, st::StateRank::Major, 80.0},
                                               {0, st::StateRank::Minor, 80.0},
                                               {1, st::StateRank::Minor, 80.0}};
    testRulers(data, m, states);
    testRelations(data, m, states);
    testUniqueNames(data, m);
    return test::result();
}
