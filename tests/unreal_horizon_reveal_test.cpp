// The reveal of the levels of the GPU horizon (agent 38, Bridge/horizon_reveal.hpp): a level shown
// comes in over its seconds, at once under the black screen; the outer band of the coarsest level
// shown dissolves while the next coarser one is hidden or coming in, never on the coarsest level;
// only the changes are sent.
#include "horizon_reveal.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using terrain::unreal::HorizonReveal;
using terrain::unreal::LevelPlacement;

LevelPlacement placement(int level, bool visible) {
    LevelPlacement result;
    result.level = level;
    result.visible = visible;
    return result;
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-9;
}

bool holds(const std::vector<int>& levels, int level) {
    return std::find(levels.begin(), levels.end(), level) != levels.end();
}

void testRevealAndEdges() {
    HorizonReveal reveal(4, 0.5);
    reveal.place(placement(0, true), false);
    TEST_CHECK(reveal.visible(0) && near(reveal.reveal(0), 0.0));
    std::vector<int> changed = reveal.advance(0.25, false);
    TEST_CHECK(changed.size() == 4); // everything sent the first time
    TEST_CHECK(near(reveal.reveal(0), 0.5) && near(reveal.edgeFade(0), 1.0));
    reveal.place(placement(1, true), false);
    changed = reveal.advance(0.25, false);
    TEST_CHECK(near(reveal.reveal(0), 1.0) && near(reveal.reveal(1), 0.5));
    TEST_CHECK(near(reveal.edgeFade(0), 0.5) && near(reveal.edgeFade(1), 1.0));
    TEST_CHECK(holds(changed, 0) && holds(changed, 1) && !holds(changed, 3));
    changed = reveal.advance(1.0, false);
    TEST_CHECK(near(reveal.reveal(1), 1.0) && near(reveal.edgeFade(0), 0.0));
    TEST_CHECK(holds(changed, 0) && holds(changed, 1));
    TEST_CHECK(reveal.advance(1.0, false).empty()); // nothing changed
    // Under the black screen a level comes in at once.
    reveal.place(placement(2, true), true);
    reveal.place(placement(3, true), false);
    static_cast<void>(reveal.advance(0.0, false));
    TEST_CHECK(near(reveal.reveal(2), 1.0) && near(reveal.edgeFade(1), 0.0));
    TEST_CHECK(near(reveal.reveal(3), 0.0) && near(reveal.edgeFade(2), 1.0));
    TEST_CHECK(near(reveal.edgeFade(3), 0.0)); // the coarsest level: no edge
    static_cast<void>(reveal.advance(0.1, true));
    TEST_CHECK(near(reveal.reveal(3), 1.0) && near(reveal.edgeFade(2), 0.0));
    // A level hidden again (a jump) is at 0 and dissolves the edge of the finer one.
    reveal.place(placement(1, false), false);
    TEST_CHECK(!reveal.visible(1) && near(reveal.reveal(1), 0.0) && near(reveal.edgeFade(0), 1.0));
}

void testAtOnceAndInvalid() {
    HorizonReveal reveal(2, 0.0);
    reveal.place(placement(0, true), false);
    static_cast<void>(reveal.advance(0.0, false));
    TEST_CHECK(near(reveal.reveal(0), 1.0));
    bool thrown = false;
    try {
        static_cast<void>(reveal.reveal(2));
    } catch (const std::out_of_range&) {
        thrown = true;
    }
    TEST_CHECK(thrown);
}

} // namespace

int main() {
    testRevealAndEdges();
    testAtOnceAndInvalid();
    return test::result();
}
