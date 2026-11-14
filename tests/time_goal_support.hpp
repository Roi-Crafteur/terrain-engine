#pragma once

// The goals in time of the tests (agent 42). A test never asserts an absolute time: a loaded
// machine misses it, and the test proves the behavior instead (an order, a count, a ratio of two
// times measured in the same test). It prints each measure against its goal; the bench built from
// the same source with TERRAIN_TIME_GOALS (tests/guard_tests.cmake: <test>_bench, run by hand on
// a quiet machine) also fails when a goal is missed.

#include <cstdio>

namespace time_goal {

inline int& missedGoals() {
    static int missed = 0;
    return missed;
}

// Prints value against its goal (value < goal), in unit; counted as missed by the bench only.
inline void report(const char* what, double value, double goal, const char* unit) {
    const bool met = value < goal;
    std::printf("time goal: %s %.2f %s, goal under %.0f %s: %s\n", what, value, unit, goal, unit,
                met ? "met" : "missed");
#ifdef TERRAIN_TIME_GOALS
    missedGoals() += met ? 0 : 1;
#endif
}

// The exit code: the result of the test, and with TERRAIN_TIME_GOALS a goal missed fails too.
inline int result(int testResult) {
    return testResult != 0 ? testResult : (missedGoals() > 0 ? 1 : 0);
}

} // namespace time_goal
