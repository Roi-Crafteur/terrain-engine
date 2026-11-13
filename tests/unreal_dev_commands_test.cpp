// The commands that edit the world as a click (agent 41, Bridge/dev_commands.hpp): the arguments
// of Terrain.EditAim and Terrain.EditRead and every refusal, the line an EditAim writes in the log,
// the sphere of a radius given and the point a trace must hit (Bridge/block_targeting.hpp).
#include "block_targeting.hpp"
#include "dev_commands.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace {

using Args = std::vector<std::string>;
using terrain::unreal::BuildAction;
using terrain::unreal::BuildTool;
using terrain::unreal::CorePoint;
using terrain::unreal::EditAimCommand;
using terrain::unreal::EditAimOutcome;

std::optional<EditAimCommand> aim(const Args& args, std::string& error) {
    return terrain::unreal::parseEditAim(args, error);
}

bool refused(const Args& args) {
    std::string error;
    const bool none = !aim(args, error).has_value();
    return none && error.starts_with("expected Terrain.EditAim");
}

void testEditAim() {
    std::string error;
    const auto dig = aim({"dig", "block"}, error);
    TEST_CHECK(dig && dig->action == BuildAction::Break && dig->tool == BuildTool::Block &&
               dig->radius == 0.0 && !dig->at);
    const auto sphere = aim({"place", "sphere"}, error);
    TEST_CHECK(sphere && sphere->action == BuildAction::Place &&
               sphere->tool == BuildTool::Sphere2 && sphere->radius == 26.0 / 16.0);
    const auto large = aim({"dig", "sphere", "2.4375"}, error);
    TEST_CHECK(large && large->radius == 39.0 / 16.0 && !large->at);
    const auto rounded = aim({"dig", "sphere", "1.53"}, error);
    TEST_CHECK(rounded && rounded->radius == 1.5); // to the sixteenth: 24 / 16
    // A point of the core, negative coordinates included, with or without a radius.
    const auto at = aim({"dig", "block", "at", "-240628", "6.18", "-86677"}, error);
    TEST_CHECK(at && at->at && at->at->x == -240628.0 && at->at->y == 6.18 &&
               at->at->z == -86677.0);
    const auto both = aim({"place", "sphere", "2", "at", "-6185116.5", "181", "155968"}, error);
    TEST_CHECK(both && both->radius == 2.0 && both->at && both->at->x == -6185116.5);
    // Refusals: missing arguments, unknown words, a radius for a block, a radius out of range,
    // "at" without three numbers, numbers that are not JSON, anything after the point.
    TEST_CHECK(refused({}));
    TEST_CHECK(refused({"dig"}));
    TEST_CHECK(refused({"break", "block"}));
    TEST_CHECK(refused({"dig", "cube"}));
    TEST_CHECK(refused({"dig", "block", "2"}));
    TEST_CHECK(refused({"dig", "sphere", "0"}));
    TEST_CHECK(refused({"dig", "sphere", "0.01"}));
    TEST_CHECK(refused({"dig", "sphere", "-1"}));
    TEST_CHECK(refused({"dig", "sphere", "33"}));
    TEST_CHECK(refused({"dig", "sphere", "big"}));
    TEST_CHECK(refused({"dig", "block", "at"}));
    TEST_CHECK(refused({"dig", "block", "at", "1", "2"}));
    TEST_CHECK(refused({"dig", "block", "at", "1", "2", "z"}));
    TEST_CHECK(refused({"dig", "block", "at", "+1", "2", "3"}));
    TEST_CHECK(refused({"dig", "block", "at", "1", "2", "3", "4"}));
    TEST_CHECK(refused({"dig", "sphere", "2", "3"}));
    TEST_CHECK(refused({"Dig", "block"}));
}

void testEditAimLine() {
    std::string error;
    const auto block = aim({"dig", "block", "at", "-240628", "6.18", "-86677"}, error);
    EditAimOutcome done;
    done.hit = CorePoint{-240628.0, 6.18, -86677.0};
    done.edit = terrain::VolumeEdit{};
    done.edit->shape = terrain::EditShape::Box;
    done.edit->anchor = {-240628, 6, -86677};
    TEST_CHECK(terrain::unreal::editAimLine(*block, done) ==
               "EditAim dig block 1 m at (-240628.00, 6.18, -86677.00): hit (-240628.00, 6.18, "
               "-86677.00), block (-240628, 6, -86677), accepted");
    const auto place = aim({"place", "block"}, error);
    EditAimOutcome capsule = done;
    capsule.edit->anchor = {-240628, 7, -86677};
    capsule.refusal = "it would close on the pawn";
    TEST_CHECK(terrain::unreal::editAimLine(*place, capsule) ==
               "EditAim place block 1 m at the center of the screen: hit (-240628.00, 6.18, "
               "-86677.00), block (-240628, 7, -86677), refused: it would close on the pawn");
    const auto sphere = aim({"dig", "sphere", "2.4375"}, error);
    EditAimOutcome round;
    round.hit = CorePoint{1.0, 2.0, 3.0};
    round.edit = terrain::VolumeEdit{};
    round.edit->shape = terrain::EditShape::Sphere;
    round.edit->anchor = {1, 2, 3};
    round.edit->radius = 39;
    TEST_CHECK(terrain::unreal::editAimLine(*sphere, round) ==
               "EditAim dig sphere 5 m at the center of the screen: hit (1.00, 2.00, 3.00), "
               "sphere at (1, 2, 3) radius 2.438 m, accepted");
    EditAimOutcome loading;
    loading.refusal = "the view is loading";
    TEST_CHECK(terrain::unreal::editAimLine(*block, loading) ==
               "EditAim dig block 1 m at (-240628.00, 6.18, -86677.00): hit none, refused: the "
               "view is loading");
}

void testEditRead() {
    std::string error;
    const auto state = terrain::unreal::parseEditRead(Args{}, error);
    TEST_CHECK(state && !state->read);
    const auto on = terrain::unreal::parseEditRead(Args{"1"}, error);
    TEST_CHECK(on && on->read && *on->read);
    const auto off = terrain::unreal::parseEditRead(Args{"0"}, error);
    TEST_CHECK(off && off->read && !*off->read);
    for (const Args& bad : {Args{"2"}, Args{"yes"}, Args{"1", "0"}}) {
        error.clear();
        TEST_CHECK(!terrain::unreal::parseEditRead(bad, error) &&
                   error == "expected Terrain.EditRead [0|1]");
    }
}

// The sphere of a radius given is the tool's at the tool's radius; another radius is rounded to
// the sixteenth of the edit; a trace edits only within 0.25 m of the point it was aimed through.
void testTargeting() {
    using terrain::unreal::buildTarget;
    const terrain::unreal::BuildHit hit{{10.2, 5.4, -3.7}, {0.0, 1.0, 0.0}, 3.0};
    const auto tool = buildTarget(hit, BuildTool::Sphere4, BuildAction::Break);
    const auto given = buildTarget(hit, BuildTool::Sphere2, BuildAction::Break, 39.0 / 16.0);
    TEST_CHECK(tool.edit == given.edit);
    const auto other = buildTarget(hit, BuildTool::Sphere2, BuildAction::Place, 1.5);
    TEST_CHECK(other.edit.radius == 24 && other.ghost.halfX == 1.5);
    const auto block = buildTarget(hit, BuildTool::Block, BuildAction::Break, 7.0);
    TEST_CHECK(block.edit == buildTarget(hit, BuildTool::Block, BuildAction::Break).edit);
    TEST_CHECK(terrain::unreal::shapeName(BuildTool::Sphere2, 39.0 / 16.0) == "sphere 5 m");
    TEST_CHECK(terrain::unreal::shapeName(BuildTool::Sphere2, 1.5) == "sphere of radius 1.5 m");
    TEST_CHECK(terrain::unreal::shapeName(BuildTool::Block, 3.0) == "block 1 m");
    TEST_CHECK(terrain::unreal::aimsAt(hit, {10.2, 5.4, -3.7}));
    TEST_CHECK(terrain::unreal::aimsAt(hit, {10.2, 5.6, -3.7}));
    TEST_CHECK(terrain::unreal::aimsAt(hit, {10.35, 5.4, -3.55}));
    TEST_CHECK(!terrain::unreal::aimsAt(hit, {10.2, 5.7, -3.7}));
    TEST_CHECK(!terrain::unreal::aimsAt(hit, {10.7, 5.4, -3.7}));
}

} // namespace

int main() {
    testEditAim();
    testEditAimLine();
    testEditRead();
    testTargeting();
    return test::result();
}
