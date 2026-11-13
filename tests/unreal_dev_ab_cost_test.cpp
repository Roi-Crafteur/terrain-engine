// The measure of Terrain.Cost (Bridge/dev_ab_cost.hpp, agent 40): its arguments, the order of its
// phases (A B B A A B), the frames thrown away after each switch, the means of the phases, of A and
// of B, their spread and their difference, on frames made here.
#include "dev_ab_cost.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace {

using terrain::unreal::AbCost;
using terrain::unreal::AbCostParams;
using Args = std::vector<std::string>;

bool near(double a, double b) {
    return std::abs(a - b) <= 1e-9;
}

void testArguments() {
    std::string error;
    const auto plain = terrain::unreal::parseCost(
        Args{"Terrain.HorizonShadows", "off", "|", "Terrain.HorizonShadows", "on"}, error);
    TEST_CHECK(plain && plain->a == "Terrain.HorizonShadows off" &&
               plain->b == "Terrain.HorizonShadows on" && plain->params.rounds == 3 &&
               plain->params.measureSeconds == 4.0);
    const auto timed =
        terrain::unreal::parseCost(Args{"6", "4", "r.Fog", "0|r.Fog", "1"}, error);
    TEST_CHECK(timed && timed->params.measureSeconds == 6.0 && timed->params.rounds == 4 &&
               timed->a == "r.Fog 0" && timed->b == "r.Fog 1");
    const auto seconds = terrain::unreal::parseCost(Args{"2.5", "A", "|", "B"}, error);
    TEST_CHECK(seconds && seconds->params.measureSeconds == 2.5 && seconds->params.rounds == 3);
    for (const Args& bad : {Args{}, Args{"A", "B"}, Args{"A", "|"}, Args{"|", "B"},
                            Args{"A", "|", "B", "|", "C"}, Args{"0.1", "A", "|", "B"},
                            Args{"4", "2.5", "A", "|", "B"}, Args{"4", "11", "A", "|", "B"}}) {
        error.clear();
        TEST_CHECK(!terrain::unreal::parseCost(bad, error) && error.starts_with("expected"));
    }
}

void testOrder() {
    const std::vector<int> expected{0, 1, 1, 0, 0, 1, 1, 0};
    for (int phase = 0; phase < 8; ++phase) {
        TEST_CHECK(AbCost::sideOf(phase) == expected[static_cast<std::size_t>(phase)]);
    }
}

// Frames every 10 ms: A costs 5 ms, B 6 ms, the warm-up of each phase 50 ms (thrown away).
void testMeans() {
    const AbCostParams params{3, 0.5, 1.0};
    AbCost cost(params);
    int side = cost.start(0.0);
    TEST_CHECK(side == 0);
    std::vector<int> run{side};
    double phaseStart = 0.0;
    double t = 0.0;
    while (!cost.done() && t < 100.0) {
        t += 0.01;
        const bool warm = t - phaseStart < params.warmupSeconds;
        const double drift = 0.001 * t; // the same for both sides over a round
        const double gpu = warm ? 50.0 : (side == 0 ? 5.0 : 6.0) + drift;
        if (const std::optional<int> next = cost.addFrame(t, gpu)) {
            side = *next;
            phaseStart = t;
            run.push_back(side);
        }
    }
    TEST_CHECK(cost.done() && run == (std::vector<int>{0, 1, 1, 0, 0, 1}));
    const terrain::unreal::AbSummary s = cost.summary();
    TEST_CHECK(s.phases.size() == 6);
    for (const terrain::unreal::AbPhase& p : s.phases) {
        TEST_CHECK(p.frames >= 99 && p.frames <= 101 && p.meanGpuMs < 7.0);
    }
    // The drift is the same on A and B over the A B B A A B order: 1 ms of difference left.
    TEST_CHECK(std::abs(s.difference - 1.0) < 1e-3);
    TEST_CHECK(s.mean[0] > 5.0 && s.mean[0] < 5.01 && s.spread[0] < 0.01);
    TEST_CHECK(!cost.addFrame(t + 10.0, 1.0));
    TEST_CHECK(near(cost.summary().difference, s.difference));
}

} // namespace

int main() {
    testArguments();
    testOrder();
    testMeans();
    return test::result();
}
