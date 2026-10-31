// The kingdoms of the default world in cells of 65,536 m (generation/settlement/kingdoms.hpp), for
// the three presets of the data: the tests of a good world of docs/PEUPLEMENT.md (each people has
// a state, no kingdom above 15 % of the land, no people above 40 % of the land held, at least half
// of the land borders natural, the capitals in the most habitable fifth near fresh water, the
// deserts, the ice and the high mountains nearly empty but for the peoples that like them: at most
// a tenth of them held by peoples that do not like them), and the same bits whatever the threads.

#include "settlement_world_support.hpp"
#include "terrain/generation/settlement/kingdoms.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

namespace st = terrain::settlement;

namespace {

void report(const char* preset, const st::Kingdoms& k, const st::SettlementData& data) {
    const st::KingdomStats& s = k.stats;
    const st::BorderStats& b = s.borders;
    std::printf("%s: %zu states (%d major, %d minor drawn), civilized %.3f, held %.3f, %d reruns, "
                "%zu pushes\n",
                preset, k.states.size(), k.preset.majorKingdoms, k.preset.minorStates,
                k.preset.civilizedShare, s.heldShare, k.reruns, k.pushes);
    std::printf("  largest state %.3f of the land, largest people %s %.3f of the land held, %d "
                "peoples without land\n",
                s.largestState,
                s.largestPeopleIndex >= 0
                    ? data.peoples[static_cast<std::size_t>(s.largestPeopleIndex)].id.c_str()
                    : "-",
                s.largestPeople, s.peoplesWithoutState);
    std::printf("  borders %zu edges, natural %.3f (river %zu, ridge %zu, coast %zu), between "
                "states %zu natural %.3f\n",
                b.edges, b.naturalShare(), b.river, b.ridge, b.coast, b.betweenStates,
                b.naturalShareBetweenStates());
    std::printf("  capitals failing %d; hostile land %zu cells, held %.3f, by peoples not liking "
                "it %.3f\n",
                s.capitalFails, s.hostileCells, s.hostileHeld, s.hostileUnliked);
    std::printf("  cuts %zu: border edges near them %zu of %zu (%.4f), land edges near them %.4f, "
                "along them %zu\n",
                k.cuts.size(), b.nearCuts, b.edges,
                b.edges > 0 ? static_cast<double>(b.nearCuts) / static_cast<double>(b.edges) : 0.0,
                b.landEdges > 0 ? static_cast<double>(b.landEdgesNearCuts) /
                                      static_cast<double>(b.landEdges)
                                : 0.0,
                b.alongCuts);
    std::printf("  seconds: capitals %.2f, expansion %.2f, cleanup %.2f, analysis %.2f\n",
                k.timings.capitals, k.timings.expansion, k.timings.cleanup, k.timings.analysis);
}

void checkGoodWorld(const st::Kingdoms& k) {
    const st::KingdomStats& s = k.stats;
    TEST_CHECK(s.peoplesWithoutState == 0);
    TEST_CHECK(s.largestState <= 0.15);
    TEST_CHECK(s.largestPeople <= 0.40);
    TEST_CHECK(s.borders.naturalShare() >= 0.50);
    TEST_CHECK(s.capitalFails == 0);
    TEST_CHECK(s.hostileUnliked <= 0.10);
}

// The noise of the cost: each cost within 1 +- amplitude of the cost without it, never 0, and
// the bits of the terrain (rivers, lakes, ridges, hostile land) untouched.
void testCostNoise(const settlement_test::World& w, const st::SettlementMaps& m,
                   const st::ExpansionTerrain& t) {
    st::SettlementConfig flat = w.data.config;
    flat.expansion.costNoise.amplitude = 0.0;
    const st::ExpansionTerrain plain =
        st::buildExpansionTerrain(w.level, m, flat, settlement_test::kSeed, 0);
    const double a = w.data.config.expansion.costNoise.amplitude;
    double low = 10.0;
    double high = 0.0;
    for (std::size_t k = 0; k < t.cost.size(); ++k) {
        const double ratio = static_cast<double>(t.cost[k]) / static_cast<double>(plain.cost[k]);
        low = std::min(low, ratio);
        high = std::max(high, ratio);
    }
    std::printf("cost noise: the costs between %.3f and %.3f times the plain ones (amplitude "
                "%.2f)\n",
                low, high, a);
    TEST_CHECK(low >= 1.0 - a - 1e-5 && high <= 1.0 + a + 1e-5 && low > 0.0);
    TEST_CHECK(high - low > a); // the noise does vary
    TEST_CHECK(plain.mask == t.mask && plain.hostile == t.hostile && plain.ford == t.ford);
}

// Every ridge edge is on a crest line: the land falls on both sides of it.
void testCrests(const settlement_test::World& w, const st::ExpansionTerrain& t) {
    const terrain::PlanetGrid& g = t.grid;
    std::size_t ridges = 0;
    std::size_t slopes = 0;
    t.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        for (const bool east : {true, false}) {
            if ((t.mask[slot] & (east ? st::kRidgeEastBit : st::kRidgeSouthBit)) == 0) {
                continue;
            }
            const auto at = [&](int k) {
                return w.level.elevation(g.index(g.wrapColumn(g.column(cell) + (east ? k : 0)),
                                                 g.wrapRow(g.row(cell) + (east ? 0 : k))));
            };
            ++ridges;
            slopes += at(-1) < at(0) && at(2) < at(1) ? 0 : 1;
        }
    });
    std::printf("ridge edges %zu, on a slope %zu\n", ridges, slopes);
    TEST_CHECK(ridges > 0 && slopes == 0);
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    const settlement_test::World w;
    const st::SettlementMaps m =
        st::computeSettlementMaps(w.level, &w.field, w.data, settlement_test::kSeed, 0);
    const st::SuitabilityMaps s =
        st::computeSuitability(w.level, m, w.data, settlement_test::kSeed, 0);
    const st::ExpansionTerrain t =
        st::buildExpansionTerrain(w.level, m, w.data.config, settlement_test::kSeed, 0);
    testCostNoise(w, m, t);
    testCrests(w, t);
    std::printf("world, maps, suitability and terrain in %.2f s\n",
                settlement_test::secondsSince(start));
    for (const st::WorldPreset& preset : w.data.config.presets) {
        const st::Kingdoms k = st::buildKingdoms(w.level, m, s, t, w.data, {preset.id, {}, {}, {}},
                                                 settlement_test::kSeed, 0);
        report(preset.id.c_str(), k, w.data);
        checkGoodWorld(k);
        if (preset.id == w.data.config.defaultPreset) {
            // The same bits whatever the threads, the terrain of the expansion included.
            const st::ExpansionTerrain one =
                st::buildExpansionTerrain(w.level, m, w.data.config, settlement_test::kSeed, 1);
            TEST_CHECK(one.mask == t.mask && one.cost == t.cost && one.ford == t.ford);
            const st::Kingdoms again = st::buildKingdoms(w.level, m, s, one, w.data,
                                                         {preset.id, {}, {}, {}},
                                                         settlement_test::kSeed, 3);
            TEST_CHECK(again.checksum() == k.checksum());
        }
    }
    std::printf("in %.2f s\n", settlement_test::secondsSince(start));
    return test::result();
}
