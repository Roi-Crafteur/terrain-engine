// Breaking and placing in game (agent 28): the edit of a tool at the point a trace of the camera
// hit, the face of a placed block, the ghost, the reach, the tools of the keys and the wheel, and
// the same edit wherever the world lies: near the origin, 20,000 km away, and across the seam of
// the wrapped world, with the origin of Unreal moved (ApplyWorldOffset, shifted).
#include "block_targeting.hpp"

#include "test_assert.hpp"

#include "terrain/storage/modifications.hpp"
#include "terrain/storage/volume_edit.hpp"
#include "terrain/world_size.hpp"

#include <cmath>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::unreal::BuildAction;
using terrain::unreal::BuildHit;
using terrain::unreal::BuildTarget;
using terrain::unreal::BuildTool;
using terrain::unreal::CoreDirection;
using terrain::unreal::CorePoint;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

BuildHit hitAt(double x, double y, double z, CoreDirection normal, double distance = 3.0) {
    return {{x, y, z}, normal, distance};
}

void testTools() {
    TEST_CHECK(terrain::unreal::toolRadius(BuildTool::Sphere2) == 1.0);
    TEST_CHECK(terrain::unreal::toolRadius(BuildTool::Sphere4) == 2.0);
    TEST_CHECK(terrain::unreal::toolRadius(BuildTool::Block) == 0.0);
}

void testFaces() {
    using terrain::unreal::dominantFace;
    TEST_CHECK(dominantFace({0, 1, 0}) == (BlockCoord{0, 1, 0}));
    TEST_CHECK(dominantFace({0, -1, 0}) == (BlockCoord{0, -1, 0}));
    TEST_CHECK(dominantFace({1, 0, 0}) == (BlockCoord{1, 0, 0}));
    TEST_CHECK(dominantFace({-1, 0, 0}) == (BlockCoord{-1, 0, 0}));
    TEST_CHECK(dominantFace({0, 0, 1}) == (BlockCoord{0, 0, 1}));
    TEST_CHECK(dominantFace({0, 0, -1}) == (BlockCoord{0, 0, -1}));
    TEST_CHECK(dominantFace({0.6, 0.3, -0.74}) == (BlockCoord{0, 0, -1})); // a steep wall
    TEST_CHECK(dominantFace({0.5, 0.5, 0.0}) == (BlockCoord{0, 1, 0}));    // ties: y first
    TEST_CHECK(dominantFace({0.5, 0.0, -0.5}) == (BlockCoord{1, 0, 0}));   // then x
    TEST_CHECK(dominantFace({0, 0, 0}) == (BlockCoord{0, 1, 0}));          // null: up
    TEST_CHECK(dominantFace({NAN, 0, 0}) == (BlockCoord{0, 1, 0}));
}

void testBlocks() {
    using terrain::unreal::buildTarget;
    // Flat ground at y = 10.3: the block under it is centered at 10, the one placed at 11.
    const BuildHit flat = hitAt(4.2, 10.3, -7.8, {0, 1, 0});
    const BuildTarget broken = buildTarget(flat, BuildTool::Block, BuildAction::Break);
    TEST_CHECK(broken.edit.shape == EditShape::Box && broken.edit.mode == EditMode::Remove);
    TEST_CHECK(broken.edit.anchor == (BlockCoord{4, 10, -8}));
    TEST_CHECK(broken.edit.size == (std::array<std::uint16_t, 3>{1, 1, 1}));
    const BuildTarget placed = buildTarget(flat, BuildTool::Block, BuildAction::Place);
    TEST_CHECK(placed.edit.mode == EditMode::Add);
    TEST_CHECK(placed.edit.anchor == (BlockCoord{4, 11, -8}));
    TEST_CHECK(placed.ghost.center.y == 11.0 && placed.ghost.halfX == 0.5 && !placed.ghost.sphere);
    // A wall facing west (-x) at x = -12.3: broken east of it, placed west of it.
    const BuildHit wall = hitAt(-12.3, 55.6, 3.1, {-0.9, 0.2, 0.1});
    TEST_CHECK(buildTarget(wall, BuildTool::Block, BuildAction::Break).edit.anchor ==
               (BlockCoord{-12, 55, 3}));
    TEST_CHECK(buildTarget(wall, BuildTool::Block, BuildAction::Place).edit.anchor ==
               (BlockCoord{-13, 55, 3}));
    // An overhang: the normal down, the block placed under it.
    const BuildHit roof = hitAt(0.4, -3.6, 0.4, {0, -1, 0});
    TEST_CHECK(buildTarget(roof, BuildTool::Block, BuildAction::Place).edit.anchor ==
               (BlockCoord{0, -4, 0}));
    // Negative coordinates round to the nearest lattice point, never towards zero.
    TEST_CHECK(terrain::unreal::blockOfPoint({-0.6, -1.4, -2.5}) == (BlockCoord{-1, -1, -2}));
}

void testSpheres() {
    using terrain::unreal::buildTarget;
    const BuildHit flat = hitAt(4.2, 10.3, -7.8, {0, 1, 0});
    const BuildTarget small = buildTarget(flat, BuildTool::Sphere2, BuildAction::Break);
    TEST_CHECK(small.edit.shape == EditShape::Sphere && small.edit.radius == 16);
    TEST_CHECK(small.edit.anchor == (BlockCoord{4, 10, -8}));
    TEST_CHECK(small.ghost.sphere && small.ghost.halfY == 1.0);
    const BuildTarget large = buildTarget(flat, BuildTool::Sphere4, BuildAction::Place);
    TEST_CHECK(large.edit.radius == 32 && large.edit.mode == EditMode::Add);
    TEST_CHECK(large.edit.anchor == (BlockCoord{4, 11, -8})); // 10.3 + 1, half its radius out
    // Every edit is valid for the core.
    terrain::validate(small.edit, terrain::WorldSize{});
    terrain::validate(large.edit, terrain::WorldSize{});
}

void testReachAndGhost() {
    TEST_CHECK(terrain::unreal::withinReach(hitAt(0, 0, 0, {0, 1, 0}, 6.0), 6.0));
    TEST_CHECK(!terrain::unreal::withinReach(hitAt(0, 0, 0, {0, 1, 0}, 6.01), 6.0));
    TEST_CHECK(!terrain::unreal::withinReach(hitAt(0, 0, 0, {0, 1, 0}, NAN), 6.0));
    const UnrealFrame frame{{100.0, 20.0, -50.0}};
    terrain::unreal::BuildGhost ghost;
    ghost.center = {101.0, 22.0, -48.0};
    ghost.halfX = 0.5;
    ghost.halfY = 2.0;
    ghost.halfZ = 1.0;
    const auto unreal = terrain::unreal::toUnrealGhost(frame, ghost);
    TEST_CHECK(unreal.center == (UnrealPoint{100.0, 200.0, 200.0}));
    TEST_CHECK(unreal.half == (UnrealPoint{50.0, 100.0, 200.0})); // core z is Unreal Y
}

// The same hit given in Unreal, wherever the frame puts the origin: the same edit.
BuildTarget aimFrom(const UnrealFrame& frame, const CorePoint& point, CoreDirection normal,
                    BuildTool tool, BuildAction action) {
    const UnrealPoint unreal = terrain::unreal::toUnreal(frame, point);
    const UnrealPoint n = terrain::unreal::toUnrealDirection(normal);
    const BuildHit hit =
        terrain::unreal::hitFromUnreal(frame, unreal, n.x, n.y, n.z, 250.0);
    TEST_CHECK(std::fabs(hit.distance - 2.5) < 1e-12);
    return terrain::unreal::buildTarget(hit, tool, action);
}

void testFarFromOrigin() {
    const CoreDirection up{0.1, 0.95, -0.2};
    for (const double far : {20'000'000.0, -20'000'000.0}) {
        const CorePoint point{far + 0.3, 1234.4, -far / 2.0 + 0.2};
        // The player stands near the point; the origin of Unreal was moved by the world offset.
        UnrealFrame frame{{far - 37.0, 1200.0, -far / 2.0 + 12.0}};
        frame = terrain::unreal::shifted(frame, {-123456.0, 98765.0, -432.0});
        const BuildTarget target =
            aimFrom(frame, point, up, BuildTool::Block, BuildAction::Place);
        const BlockCoord expected{static_cast<int>(far), 1235, static_cast<int>(-far / 2.0)};
        TEST_CHECK(target.edit.anchor == expected);
        TEST_CHECK(aimFrom(frame, point, up, BuildTool::Sphere4, BuildAction::Break).edit.anchor ==
                   (BlockCoord{expected.x, 1234, expected.z}));
    }
}

void testSeam() {
    // Across the seam of the wrapped world (x = +-30,015,488 m): the hit on one side or on the
    // image of the other side gives the same blocks in the core.
    const terrain::WorldSize world;
    const double half = world.x / 2.0;
    const CoreDirection up{0, 1, 0};
    const UnrealFrame east{{half - 3.0, 40.0, 0.0}};
    const UnrealFrame west{{-half - 3.0, 40.0, 0.0}};
    const BuildTarget a = aimFrom(east, {half + 0.2, 41.3, 5.1}, up, BuildTool::Sphere4,
                                  BuildAction::Break);
    const BuildTarget b = aimFrom(west, {-half + 0.2, 41.3, 5.1}, up, BuildTool::Sphere4,
                                  BuildAction::Break);
    TEST_CHECK(a.edit.anchor.x - b.edit.anchor.x == static_cast<int>(world.x));
    terrain::ModificationStore storeA;
    terrain::ModificationStore storeB;
    const auto chunksA = terrain::applyVolumeEdit(storeA, a.edit, world);
    const auto chunksB = terrain::applyVolumeEdit(storeB, b.edit, world);
    TEST_CHECK(chunksA == chunksB);
    TEST_CHECK(storeA == storeB);
    TEST_CHECK(storeA.blockCount() > 0);
    // Its chunks lie on both sides of the seam.
    bool west0 = false;
    bool east0 = false;
    for (const terrain::ChunkCoord& chunk : chunksA) {
        west0 = west0 || chunk.x < 0;
        east0 = east0 || chunk.x >= 0;
    }
    TEST_CHECK(west0 && east0);
}

} // namespace

int main() {
    testTools();
    testFaces();
    testBlocks();
    testSpheres();
    testReachAndGhost();
    testFarFromOrigin();
    testSeam();
    return test::result();
}
