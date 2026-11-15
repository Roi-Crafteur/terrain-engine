// The coasts under the sea and the valleys above it (coast_valleys.hpp; agent 31, task 11C): a
// fabricated case of the stacks of cote_stack, the floor of the sea at -13 m under the bare relief,
// a valley whose floor stands at +70 m less than 60 m away (inside its corridor, or at its edge).
// With HeightfieldParams::seaKeepsCoasts the coast and its stack stay whole; without, they fade out
// or vanish (the cause stays covered). The other way round, a valley above the water before a
// coast out of it: the coast fades out as before, with the setting or without. And a profile across
// a coast, fabricated: a cove cut into the cliff under a valley perched on the plateau passes with
// the setting (the reefs and stacks between the coves stay), while the corridor of the valley keeps
// the relief of the valley: no water of the river over a hole, its bed whole.
#include "coast_valleys.hpp"

#include "terrain/generation/experimental_flags.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using terrain::Hydrology;
using terrain::coast_valleys::covePasses;
using terrain::coast_valleys::faceWeight;
using terrain::coast_valleys::ValleyFade;
using terrain::coast_valleys::valleyBlend;
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

// The profile: x meters inland from the shore. The relief without the coasts rises from the floor
// of the sea (-13 m) to a plateau at 75 m; the coast is a cliff at the shore, and a cove cut into
// it up to 150 m inland (its floor at -5 m); a valley runs along the plateau, its corridor from 180
// to 260 m inland, its floor at 66 m, its water at 67 m.
double bareAt(double x) {
    return std::clamp(-13.0 + (x + 200.0) * 0.22, -13.0, 75.0);
}

double coastAt(double x, bool cove) {
    if (x < 0.0) {
        return -13.0;
    }
    return cove && x < 150.0 ? -5.0 : 75.0;
}

Hydrology::Clearance valleyNear(double x) {
    const double away = x < 180.0 ? 180.0 - x : (x > 260.0 ? x - 260.0 : -std::min(x - 180.0, 260.0 - x));
    return {away, 66.0};
}

// Whether the cove may be cut: every point of it as far from the rivers as the inlets want
// (coast_inlets.cpp: kRiverClear 100 m plus its widest half, 30 m here), or under a perched valley.
bool coveCut(bool seaKeeps) {
    for (double x = 0.0; x < 150.0; x += 5.0) {
        const Hydrology::Clearance near = valleyNear(x);
        const double clear = covePasses(seaKeeps, bareAt(x), near.floor)
                                 ? std::numeric_limits<double>::infinity()
                                 : near.over(bareAt(x));
        if (clear < 130.0) {
            return false;
        }
    }
    return true;
}

// The relief at x once the coasts fade out before the valleys (Heightfield::leaveValleys).
double reliefAt(double x, bool cove, bool seaKeeps) {
    const auto blend = valleyBlend(valleyNear(x), bareAt(x), kClearance, seaKeeps);
    if (blend.fade == ValleyFade::Coast) {
        return coastAt(x, cove);
    }
    if (blend.fade == ValleyFade::Bare) {
        return bareAt(x);
    }
    return bareAt(x) + static_cast<double>(blend.weight) * (coastAt(x, cove) - bareAt(x));
}

void testProfile() {
    TEST_CHECK(!coveCut(false)); // as main: no cove, no reef, no stack under the valley
    TEST_CHECK(coveCut(true));   // the cove under the perched valley, its reefs and stacks
    const bool cove = coveCut(true);
    double deepest = 1.0e9;
    bool bedWhole = true;
    for (double x = -200.0; x <= 300.0; x += 1.0) {
        const double h = reliefAt(x, cove, true);
        if (x >= 180.0 && x <= 260.0) {
            // The corridor keeps the relief of the valley, never cut by the coast: the river's
            // bed (66 m) and its water (67 m) lie on it, neither over a hole nor cut short.
            bedWhole = bedWhole && h == bareAt(x) && h >= 67.0;
        }
        if (x >= 0.0 && x < 150.0) {
            deepest = std::min(deepest, h);
        }
    }
    std::printf("profile: the cove down to %.1f m under the perched valley, the corridor whole: %s\n",
                deepest, bedWhole ? "yes" : "no");
    TEST_CHECK(bedWhole);
    TEST_CHECK(deepest < 0.0); // the cove holds the sea, out of the corridor
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
    testProfile();
    testFlags();
    return test::result();
}
