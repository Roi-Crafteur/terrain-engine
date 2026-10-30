// The method of cost_ratio_support.hpp on simulated times: the best time of each item on each
// side, the order of the pairs, a ratio measured again only past its limit with the best times
// kept, so that a slowdown is undone and a true regression still fails, and the text of a ratio.
#include "cost_ratio_support.hpp"

#include "test_assert.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

namespace {

using cost_test::Ratio;
using cost_test::Side;

constexpr std::size_t kItems = 4;
constexpr std::size_t kCallsPerRound = 2 * kItems;

// A simulated machine: item i costs i + 1 seconds without the feature and factor times that with
// it, plus the delay slow() gives to a call by its round (0 is the warm-up), its side and its
// item. It records its calls.
struct Machine {
    double factor = 1.5;
    std::function<double(std::size_t atRound, Side side, std::size_t item)> slow;
    std::vector<std::pair<Side, std::size_t>> calls;

    cost_test::TimedRun run() {
        return [this](Side side, std::size_t item) {
            const std::size_t atRound = calls.size() / kCallsPerRound;
            calls.emplace_back(side, item);
            const double base = static_cast<double>(item + 1);
            const double delay = slow ? slow(atRound, side, item) : 0.0;
            return (side == Side::With ? factor * base : base) + delay;
        };
    }
};

// Without any slowdown: the true ratio, measured once after the warm-up and 5 rounds.
void testExact() {
    Machine m;
    const Ratio r = cost_test::ratioAtMost(kItems, m.run(), 2.0);
    TEST_CHECK(r.measured == std::vector<double>{1.5});
    TEST_CHECK(r.passed() && r.most == 2.0);
    TEST_CHECK(m.calls.size() == (1 + 5) * kCallsPerRound);

    Machine alone;
    cost_test::BestTimes best(kItems, alone.run());
    best.warmUp();
    TEST_CHECK(best.rounds() == 0 && alone.calls.size() == kCallsPerRound);
    best.addRounds(2);
    TEST_CHECK(best.rounds() == 2 && best.ratio() == 1.5);
    TEST_CHECK(alone.calls.size() == 3 * kCallsPerRound);
}

// Slowed calls on both sides, never every round of an item on a side: ignored.
void testSlowCalls() {
    Machine m;
    m.slow = [](std::size_t atRound, Side side, std::size_t item) {
        return (atRound + item + (side == Side::With ? 1 : 0)) % 3 == 0 ? 100.0 : 0.0;
    };
    const Ratio r = cost_test::ratioAtMost(kItems, m.run(), 2.0);
    TEST_CHECK(r.measured == std::vector<double>{1.5});
}

// Each round runs every item on both sides back to back, Without first when the round and the
// item have the same parity.
void testOrder() {
    Machine m;
    static_cast<void>(cost_test::ratioAtMost(kItems, m.run(), 2.0));
    TEST_CHECK(m.calls.size() == 6 * kCallsPerRound);
    for (std::size_t c = 0; c + 1 < m.calls.size(); c += 2) {
        const std::size_t atRound = c / kCallsPerRound;
        const std::size_t item = (c % kCallsPerRound) / 2;
        const Side first = (atRound + item) % 2 == 0 ? Side::Without : Side::With;
        const Side second = first == Side::Without ? Side::With : Side::Without;
        TEST_CHECK(m.calls[c] == std::pair{first, item});
        TEST_CHECK(m.calls[c + 1] == std::pair{second, item});
    }
}

// The side with the feature slowed during the whole first measure: past the limit, the rounds
// measured again find the true ratio and the test passes.
void testMeasuredAgain() {
    Machine m;
    m.slow = [](std::size_t atRound, Side side, std::size_t) {
        return side == Side::With && atRound <= 5 ? 100.0 : 0.0;
    };
    const Ratio r = cost_test::ratioAtMost(kItems, m.run(), 2.0);
    TEST_CHECK(r.measured == (std::vector<double>{41.5, 1.5}));
    TEST_CHECK(r.passed());
    TEST_CHECK(m.calls.size() == (1 + 10) * kCallsPerRound);
}

// A true regression (x2.5) fails, even with the times without the feature slowed in four rounds
// out of five: measured three times, each time the true ratio.
void testRegression() {
    Machine m;
    m.factor = 2.5;
    m.slow = [](std::size_t atRound, Side side, std::size_t item) {
        return side == Side::Without && (atRound + item) % 5 != 0 ? 100.0 : 0.0;
    };
    const Ratio r = cost_test::ratioAtMost(kItems, m.run(), 2.0);
    TEST_CHECK(r.measured == (std::vector<double>{2.5, 2.5, 2.5}));
    TEST_CHECK(!r.passed());
    TEST_CHECK(m.calls.size() == (1 + 15) * kCallsPerRound);

    Machine once;
    once.factor = 2.5;
    const Ratio first = cost_test::ratioAtMost(kItems, once.run(), 2.0, {3, 0});
    TEST_CHECK(first.measured == std::vector<double>{2.5} && !first.passed());
    TEST_CHECK(once.calls.size() == (1 + 3) * kCallsPerRound);
}

// No item: no ratio, which fails.
void testNoItem() {
    Machine m;
    const Ratio r = cost_test::ratioAtMost(0, m.run(), 2.0);
    TEST_CHECK(r.measured.size() == 3 && std::isnan(r.measured.back()) && !r.passed());
    TEST_CHECK(m.calls.empty());
    TEST_CHECK(!Ratio{}.passed());
}

void testText() {
    TEST_CHECK(cost_test::ratioText({{1.4549}, 2.0}) == "x1.45 (at most x2.0)");
    TEST_CHECK(cost_test::ratioText({{2.031, 1.524}, 2.0}) ==
               "x2.03, measured again x1.52 (at most x2.0)");
    TEST_CHECK(cost_test::ratioText({{1.41, 1.36, 1.33}, 1.3}) ==
               "x1.41, measured again x1.36, x1.33 (at most x1.3)");
}

// secondsOf: at least the time slept.
void testSecondsOf() {
    const double seconds = cost_test::secondsOf(
        [] { std::this_thread::sleep_for(std::chrono::milliseconds(2)); });
    TEST_CHECK(seconds >= 0.002 && seconds < 10.0);
}

} // namespace

int main() {
    testExact();
    testSlowCalls();
    testOrder();
    testMeasuredAgain();
    testRegression();
    testNoItem();
    testText();
    testSecondsOf();
    return test::result();
}
