// The coasts under the sea and the valleys above it (coast_valleys.hpp; agent 31, task 11C): a
// fabricated case of the stacks of cote_stack, the floor of the sea at -13 m under the bare relief,
// a valley whose floor stands at +70 m less than 60 m away (inside its corridor, or at its edge).
// With HeightfieldParams::seaKeepsCoasts the coast and its stack stay whole; without, they fade out
// or vanish (the cause stays covered). The other way round, a valley above the water before a
// coast out of it: the coast fades out as before, with the setting or without.
#include "coast_valleys.hpp"

#include "terrain/generation/experimental_flags.hpp"

#include "test_assert.hpp"

#include <cstdio>
#include <limits>

namespace {

using terrain::Hydrology;
using terrain::coast_valleys::faceWeight;
using terrain::coast_valleys::seaKeepsCoast;

constexpr double kClearance = 60.0; // as heightfield_coast.cpp

void testUnderTheSea() {
    const double seaFloor = -13.0;
    // Inside the corridor of the valley (550 m in, as at cote_stack), its floor at +70 m.
    const Hydrology::Clearance inside{-550.0, 70.0};
    // At the edge of the corridor, 30 m away.
    const Hydrology::Clearance edge{30.0, 70.0};
    TEST_CHECK(seaKeepsCoast(true, seaFloor, inside.floor));
    TEST_CHECK(!seaKeepsCoast(false, seaFloor, inside.floor));
    // The stack and the coast stay whole with the setting.
    TEST_CHECK(faceWeight(inside, seaFloor, kClearance, true) == 1.0f);
    TEST_CHECK(faceWeight(edge, seaFloor, kClearance, true) == 1.0f);
    // Without it, gone inside the corridor, fading at its edge: the cause.
    TEST_CHECK(faceWeight(inside, seaFloor, kClearance, false) == 0.0f);
    const float faded = faceWeight(edge, seaFloor, kClearance, false);
    std::printf("the stack under the sea before a valley at +70 m: weight %.3f at 30 m without the "
                "setting, 1 with it\n",
                static_cast<double>(faded));
    TEST_CHECK(faded > 0.0f && faded < 1.0f);
}

void testOutOfTheWater() {
    // A coast out of the water (the top of a cliff at +5 m) before a valley at +70 m: the coast
    // fades out as before, with the setting or without.
    const Hydrology::Clearance inside{-550.0, 70.0};
    const Hydrology::Clearance edge{30.0, 70.0};
    for (const double ground : {5.0, 0.0}) {
        TEST_CHECK(!seaKeepsCoast(true, ground, inside.floor));
        TEST_CHECK(faceWeight(inside, ground, kClearance, true) ==
                   faceWeight(inside, ground, kClearance, false));
        TEST_CHECK(faceWeight(edge, ground, kClearance, true) ==
                   faceWeight(edge, ground, kClearance, false));
    }
    TEST_CHECK(faceWeight(inside, 5.0, kClearance, true) == 0.0f);
    // A valley that reaches the sea (its floor below it, an estuary): the coast under the sea fades
    // out before it as before.
    const Hydrology::Clearance estuary{-200.0, -4.0};
    TEST_CHECK(!seaKeepsCoast(true, -13.0, estuary.floor));
    TEST_CHECK(faceWeight(estuary, -13.0, kClearance, true) ==
               faceWeight(estuary, -13.0, kClearance, false));
    // No valley near: nothing changes.
    const Hydrology::Clearance none;
    TEST_CHECK(!seaKeepsCoast(true, -13.0, none.floor));
    TEST_CHECK(faceWeight(none, -13.0, kClearance, true) == 1.0f);
}

// The flags that may bring a river to a coast switch the setting on; none, as main.
void testFlags() {
    const terrain::WorldParams off;
    TEST_CHECK(!off.relief.seaKeepsCoasts);
    for (const char* flag : {"smooth_bands", "pass_routing", "sphere_latitude"}) {
        terrain::WorldParams p;
        (void)terrain::applyExperimentalFlags(flag, p);
        TEST_CHECK(p.relief.seaKeepsCoasts);
    }
    terrain::WorldParams network;
    (void)terrain::applyExperimentalFlags("planet_network", network);
    TEST_CHECK(!network.relief.seaKeepsCoasts); // data only
}

} // namespace

int main() {
    testUnderTheSea();
    testOutOfTheWater();
    testFlags();
    return test::result();
}
