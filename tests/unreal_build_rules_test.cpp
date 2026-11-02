// The rules of breaking and placing in game (agent 28): when a click may edit (the dev map, a
// load, the flying pawn of the tools, the focus), how often a held button edits, and the refusal
// of a placed volume that would close on the capsule of the pawn, near the origin and far away.
#include "build_rules.hpp"

#include "block_targeting.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstring>

namespace {

using terrain::BlockCoord;
using terrain::VolumeEdit;
using terrain::unreal::BuildGate;
using terrain::unreal::CoreCapsule;

BuildGate openGate() {
    BuildGate gate;
    gate.playing = true;
    gate.controlsOwner = true;
    gate.focused = true;
    return gate;
}

void testGate() {
    using terrain::unreal::gateRefusal;
    TEST_CHECK(gateRefusal(openGate()) == nullptr);
    BuildGate gate = openGate();
    gate.devMapOpen = true;
    TEST_CHECK(std::strcmp(gateRefusal(gate), "the dev map is open") == 0);
    gate = openGate();
    gate.loading = true;
    TEST_CHECK(std::strcmp(gateRefusal(gate), "the view is loading") == 0);
    gate = openGate();
    gate.controlsOwner = false; // the flying pawn of the tools is possessed
    TEST_CHECK(gateRefusal(gate) != nullptr);
    gate = openGate();
    gate.focused = false;
    TEST_CHECK(gateRefusal(gate) != nullptr);
    TEST_CHECK(gateRefusal(BuildGate{}) != nullptr);
}

void testRate() {
    terrain::unreal::EditRate rate(0.25);
    TEST_CHECK(rate.allow(10.0)); // the first press at once
    TEST_CHECK(!rate.allow(10.1));
    TEST_CHECK(!rate.allow(10.249));
    TEST_CHECK(rate.allow(10.25));
    // Held for 2 s at 60 frames per second: 8 edits, one every quarter of a second.
    terrain::unreal::EditRate held(0.25);
    int edits = 0;
    for (int frame = 0; frame < 120; ++frame) {
        edits += held.allow(frame / 60.0) ? 1 : 0;
    }
    TEST_CHECK(edits == 8);
}

VolumeEdit block(int x, int y, int z) {
    VolumeEdit edit;
    edit.shape = terrain::EditShape::Box;
    edit.mode = terrain::EditMode::Add;
    edit.anchor = {x, y, z};
    edit.size = {1, 1, 1};
    return edit;
}

// The capsule of the default Character: 34 cm of radius, 88 cm of half height, standing on the
// ground at y = 10.5 (the top of the block at 10) at (x, z).
CoreCapsule standing(double x, double z) {
    return {x, z, 10.5, 10.5 + 1.76, 0.34};
}

void testCapsule() {
    using terrain::unreal::capsuleDistance;
    using terrain::unreal::touchesCapsule;
    const CoreCapsule pawn = standing(0.0, 0.0);
    TEST_CHECK(touchesCapsule(block(0, 11, 0), pawn)); // in the legs
    TEST_CHECK(touchesCapsule(block(0, 12, 0), pawn)); // in the chest
    TEST_CHECK(!touchesCapsule(block(1, 11, 0), pawn)); // its cube 16 cm from the capsule
    TEST_CHECK(std::fabs(capsuleDistance(block(1, 11, 0), pawn) - 0.16) < 1e-9);
    TEST_CHECK(!touchesCapsule(block(0, 9, 0), pawn)); // under the ground under the feet
    TEST_CHECK(!touchesCapsule(block(0, 13, 0), pawn)); // over the head (12.26)
    TEST_CHECK(!touchesCapsule(block(2, 11, 0), pawn)); // 1.5 - 0.34 = 1.16 m away
    TEST_CHECK(std::fabs(capsuleDistance(block(2, 11, 0), pawn) - 1.16) < 1e-9);
    // A sphere of 2 m across centered 1 m beside the axis reaches the capsule, 3 m beside it not.
    VolumeEdit sphere;
    sphere.shape = terrain::EditShape::Sphere;
    sphere.mode = terrain::EditMode::Add;
    sphere.anchor = {1, 11, 0};
    sphere.radius = 16;
    TEST_CHECK(touchesCapsule(sphere, pawn));
    sphere.anchor = {3, 11, 0};
    TEST_CHECK(!touchesCapsule(sphere, pawn));
}

void testCapsuleFromUnreal() {
    // 20,000 km east of the origin of the core, the origin of Unreal moved near the pawn.
    const terrain::unreal::UnrealFrame frame{{20'000'000.0, 100.0, -3'000'000.0}};
    const CoreCapsule pawn =
        terrain::unreal::capsuleFromUnreal(frame, {150.0, -250.0, 1088.0}, 88.0, 34.0);
    TEST_CHECK(std::fabs(pawn.x - 20'000'001.5) < 1e-6);
    TEST_CHECK(std::fabs(pawn.z - -3'000'002.5) < 1e-6);
    TEST_CHECK(std::fabs(pawn.bottom - 110.0) < 1e-9 && std::fabs(pawn.top - 111.76) < 1e-9);
    TEST_CHECK(std::fabs(pawn.radius - 0.34) < 1e-12);
    // Placing a block on the ground just in front of its feet, aimed at by the camera.
    const terrain::unreal::BuildHit ground{{20'000'001.6, 110.2, -3'000'002.2}, {0, 1, 0}, 2.0};
    const auto target = terrain::unreal::buildTarget(ground, terrain::unreal::BuildTool::Block,
                                                     terrain::unreal::BuildAction::Place);
    TEST_CHECK(target.edit.anchor == (BlockCoord{20'000'002, 111, -3'000'002}));
    TEST_CHECK(terrain::unreal::touchesCapsule(target.edit, pawn));
    // Breaking under its feet is never refused by the capsule: the pawn falls.
    const auto under = terrain::unreal::buildTarget(ground, terrain::unreal::BuildTool::Block,
                                                    terrain::unreal::BuildAction::Break);
    TEST_CHECK(under.edit.anchor.y == 110);
}

} // namespace

int main() {
    testGate();
    testRate();
    testCapsule();
    testCapsuleFromUnreal();
    return test::result();
}
