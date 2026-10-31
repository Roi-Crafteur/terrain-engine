// The poses of the views of the tools of the developers (Bridge/place_pose.hpp) and the arguments
// of their console commands (Bridge/dev_commands.hpp): the yaw of a heading and back, the eye
// above the ground or the water, a saved view posed again where it was, and every refusal.
#include "dev_commands.hpp"
#include "place_pose.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using terrain::dev::EyeMode;
using terrain::unreal::ViewPose;

constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}

void testHeadings() {
    using terrain::unreal::headingOfYaw;
    using terrain::unreal::yawOfHeading;
    TEST_CHECK(yawOfHeading(0.0) == -90.0);   // north: -Y of Unreal
    TEST_CHECK(yawOfHeading(90.0) == 0.0);    // east: +X
    TEST_CHECK(yawOfHeading(180.0) == 90.0);  // south: +Y
    TEST_CHECK(yawOfHeading(270.0) == 180.0); // west: -X, the end of (-180, 180]
    TEST_CHECK(yawOfHeading(227.0) == 137.0);
    TEST_CHECK(yawOfHeading(359.0) == -91.0);
    TEST_CHECK(headingOfYaw(-90.0) == 0.0 && headingOfYaw(180.0) == 270.0);
    TEST_CHECK(headingOfYaw(-180.0) == 270.0 && headingOfYaw(270.0) == 0.0);
    // The forward vector of the yaw in Unreal (cos, sin along X and Y, the core x and z) is the
    // heading of the core, clockwise from -z: (sin h, -cos h).
    for (int i = 0; i < 360; ++i) {
        const double heading = i + 0.25;
        const double yaw = yawOfHeading(heading) * kPi / 180.0;
        const double h = heading * kPi / 180.0;
        TEST_CHECK(near(std::cos(yaw), std::sin(h), 1e-12));
        TEST_CHECK(near(std::sin(yaw), -std::cos(h), 1e-12));
        TEST_CHECK(near(headingOfYaw(yawOfHeading(heading)), heading, 1e-12));
    }
}

void testEyes() {
    using terrain::unreal::columnTop;
    terrain::HeightSample dry;
    dry.height = 178.82f;
    dry.waterLevel = 178.82f;
    TEST_CHECK(columnTop(dry) == static_cast<double>(178.82f));
    terrain::HeightSample basin = dry;
    basin.water = terrain::WaterKind::Lake;
    basin.waterLevel = 180.03f;
    TEST_CHECK(columnTop(basin) == static_cast<double>(180.03f));
    terrain::HeightSample sea;
    sea.height = -1.47f;
    sea.water = terrain::WaterKind::Ocean;
    sea.waterLevel = 0.0f;
    TEST_CHECK(columnTop(sea) == 0.0);
    TEST_CHECK(terrain::unreal::eyeAltitude(182.03, EyeMode::Altitude, 500.0) == 182.03);
    TEST_CHECK(terrain::unreal::eyeAltitude(2.0, EyeMode::Ground, 180.03) == 182.03);
    // The example of the format: the eye 2 m over the water of the basin of the fall of 388 m.
    terrain::dev::Place place;
    place.name = "chute388_bassin";
    place.x = -6185104.0;
    place.z = 155924.0;
    place.heading = 227.0;
    place.pitch = 30.0;
    place.eye = 182.03;
    const ViewPose pose = terrain::unreal::placePose(place, 9999.0);
    TEST_CHECK(pose.eye.x == -6185104.0 && pose.eye.y == 182.03 && pose.eye.z == 155924.0);
    TEST_CHECK(pose.yaw == 137.0 && pose.pitch == 30.0);
    place.eyeMode = EyeMode::Ground;
    place.eye = 2.0;
    TEST_CHECK(terrain::unreal::placePose(place, 180.03).eye.y == 180.03 + 2.0);
    const ViewPose steep =
        terrain::unreal::viewPose(1.0, 2.0, 0.0, 120.0, 3.0, EyeMode::Altitude, 0.0);
    TEST_CHECK(steep.pitch == 90.0 && steep.yaw == -90.0);
}

void testSavedViews() {
    const terrain::WorldSize world;
    ViewPose pose;
    pose.eye = {-6185104.004, 182.0349, 155924.0};
    pose.yaw = 137.0;
    pose.pitch = 30.004;
    const terrain::dev::Place saved =
        terrain::unreal::placeOfView("chute388_bassin", "a note", pose, true, world);
    TEST_CHECK(saved.x == -6185104.0 && saved.z == 155924.0);
    TEST_CHECK(saved.eye == 182.03 && saved.eyeMode == EyeMode::Altitude);
    TEST_CHECK(saved.heading == 227.0 && saved.pitch == 30.0 && saved.planet);
    TEST_CHECK(saved.note == "a note");
    // Saved, written, read back and posed again: the same view, whatever the ground.
    terrain::dev::PlacesFile file;
    file.put(saved);
    const terrain::dev::Place back = terrain::dev::parsePlaces(writePlaces(file)).places[0];
    TEST_CHECK(back == saved);
    const ViewPose again = terrain::unreal::placePose(back, -50.0);
    TEST_CHECK(near(again.eye.x, pose.eye.x, 0.005) && near(again.eye.y, pose.eye.y, 0.005));
    TEST_CHECK(near(again.yaw, pose.yaw, 0.005) && near(again.pitch, pose.pitch, 0.005));
    // An unwrapped position is saved at its canonical image; a heading that rounds to 360 is 0.
    pose.eye.x = world.x / 2.0 + 5.0;
    pose.eye.z = -world.z / 2.0 - 7.5;
    pose.yaw = -90.004;
    const terrain::dev::Place wrapped =
        terrain::unreal::placeOfView("edge", "", pose, false, world);
    TEST_CHECK(wrapped.x == -world.x / 2.0 + 5.0 && wrapped.z == world.z / 2.0 - 7.5);
    TEST_CHECK(wrapped.heading == 0.0);
    bool threw = false;
    try {
        (void)terrain::unreal::placeOfView("a b", "", pose, false, world);
    } catch (const terrain::dev::PlacesError&) {
        threw = true;
    }
    TEST_CHECK(threw);
}

using Args = std::vector<std::string>;

void testGotoLookEye() {
    std::string error;
    const auto go = terrain::unreal::parseGoto(Args{"-6185104", "155924.5"}, error);
    TEST_CHECK(go && go->place.empty() && go->x == -6185104.0 && go->z == 155924.5 && !go->eye);
    const auto high = terrain::unreal::parseGoto(Args{"1", "2", "182.03"}, error);
    TEST_CHECK(high && high->eye && high->eye->value == 182.03 &&
               high->eye->mode == EyeMode::Altitude);
    const auto above = terrain::unreal::parseGoto(Args{"1", "2", "+2"}, error);
    TEST_CHECK(above && above->eye->value == 2.0 && above->eye->mode == EyeMode::Ground);
    const auto named = terrain::unreal::parseGoto(Args{"k2_paroi"}, error);
    TEST_CHECK(named && named->place == "k2_paroi");
    for (const Args& bad : {Args{}, Args{"a b"}, Args{"1", "x"}, Args{"1", "2", "+-1"},
                            Args{"1", "2", "3", "4"}, Args{"1,5", "2"}, Args{"1", "2", "high"}}) {
        error.clear();
        TEST_CHECK(!terrain::unreal::parseGoto(bad, error) && error.starts_with("expected"));
    }
    const auto look = terrain::unreal::parseLook(Args{"370", "-30"}, error);
    TEST_CHECK(look && look->heading == 10.0 && look->pitch == -30.0);
    TEST_CHECK(terrain::unreal::parseLook(Args{"-10", "0"}, error)->heading == 350.0);
    TEST_CHECK(!terrain::unreal::parseLook(Args{"0", "91"}, error));
    TEST_CHECK(!terrain::unreal::parseLook(Args{"0"}, error));
    const auto eye = terrain::unreal::parseEye(Args{"+1.8"}, error);
    TEST_CHECK(eye && eye->value == 1.8 && eye->mode == EyeMode::Ground);
    TEST_CHECK(terrain::unreal::parseEye(Args{"-12.5"}, error)->value == -12.5);
    TEST_CHECK(!terrain::unreal::parseEye(Args{"+"}, error));
    TEST_CHECK(!terrain::unreal::parseEye(Args{"1", "2"}, error));
}

void testSaveShotViews() {
    std::string error;
    const auto save =
        terrain::unreal::parseSave(Args{"k2_paroi", "la", "plus", "haute", "paroi"}, error);
    TEST_CHECK(save && save->name == "k2_paroi" && save->note == "la plus haute paroi");
    TEST_CHECK(terrain::unreal::parseSave(Args{"k2"}, error)->note.empty());
    TEST_CHECK(!terrain::unreal::parseSave(Args{}, error));
    TEST_CHECK(!terrain::unreal::parseSave(Args{"../k2"}, error));
    TEST_CHECK(terrain::unreal::parseShot(Args{"lac_montagne"}, error) == "lac_montagne");
    TEST_CHECK(!terrain::unreal::parseShot(Args{"a/b"}, error));
    TEST_CHECK(!terrain::unreal::parseShot(Args{"a", "b"}, error));
    const auto views = terrain::unreal::parseViews(Args{"chute388_*", "k2_paroi"}, error);
    TEST_CHECK(views && views->size() == 2 && (*views)[0] == "chute388_*");
    TEST_CHECK(!terrain::unreal::parseViews(Args{}, error));
}

void testSeed() {
    using terrain::unreal::WorldPreset;
    std::string error;
    const auto seed = terrain::unreal::parseSeed(Args{"20261019"}, error);
    TEST_CHECK(seed && seed->seed == 20261019 && seed->preset == WorldPreset::Keep);
    TEST_CHECK(terrain::unreal::parseSeed(Args{"-1", "caves"}, error)->seed == -1);
    TEST_CHECK(terrain::unreal::parseSeed(Args{"7", "regions"}, error)->preset ==
               WorldPreset::Regions);
    TEST_CHECK(terrain::unreal::parseSeed(Args{"7", "surface"}, error)->preset ==
               WorldPreset::Surface);
    TEST_CHECK(terrain::unreal::parseSeed(Args{"9223372036854775807"}, error)->seed ==
               std::numeric_limits<std::int64_t>::max());
    TEST_CHECK(terrain::unreal::parseSeed(Args{"-9223372036854775808"}, error)->seed ==
               std::numeric_limits<std::int64_t>::min());
    for (const Args& bad : {Args{}, Args{"9223372036854775808"}, Args{"-"}, Args{"12a"},
                            Args{"1.5"}, Args{"+3"}, Args{"7", "plains"}, Args{"1", "caves", "x"},
                            Args{"99999999999999999999"}}) {
        TEST_CHECK(!terrain::unreal::parseSeed(bad, error));
    }
    TEST_CHECK(terrain::unreal::presetName(WorldPreset::Regions) == "regions");
}

void testModesAndPages() {
    using terrain::unreal::HudPage;
    using terrain::unreal::ViewMode;
    std::string error;
    TEST_CHECK(terrain::unreal::parseViewMode(Args{"filaire"}, error) == ViewMode::Wireframe);
    TEST_CHECK(terrain::unreal::parseViewMode(Args{"wireframe"}, error) == ViewMode::Wireframe);
    TEST_CHECK(terrain::unreal::parseViewMode(Args{"seche"}, error) == ViewMode::Dry);
    TEST_CHECK(terrain::unreal::parseViewMode(Args{"pente"}, error) == ViewMode::Slope);
    TEST_CHECK(!terrain::unreal::parseViewMode(Args{"wire"}, error));
    TEST_CHECK(error == "expected Terrain.View normal|filaire|lod|roche|eau|seche|normales|pente");
    for (std::size_t i = 0; i < terrain::unreal::kViewModeCount; ++i) {
        const auto mode = static_cast<ViewMode>(i);
        const std::string name(terrain::unreal::viewModeName(mode));
        TEST_CHECK(terrain::unreal::parseViewMode(Args{name}, error) == mode);
    }
    TEST_CHECK(terrain::unreal::nextHudPage(HudPage::Off) == HudPage::Player);
    TEST_CHECK(terrain::unreal::nextHudPage(HudPage::Perf) == HudPage::Off);
    TEST_CHECK(terrain::unreal::parseHud(Args{}, HudPage::Aimed, error) == HudPage::Perf);
    TEST_CHECK(terrain::unreal::parseHud(Args{"2"}, HudPage::Off, error) == HudPage::Aimed);
    TEST_CHECK(!terrain::unreal::parseHud(Args{"4"}, HudPage::Off, error));
    TEST_CHECK(!terrain::unreal::parseHud(Args{"12"}, HudPage::Off, error));
    TEST_CHECK(terrain::unreal::hudPageName(HudPage::Perf) == "perf");
}

} // namespace

int main() {
    testHeadings();
    testEyes();
    testSavedViews();
    testGotoLookEye();
    testSaveShotViews();
    testSeed();
    testModesAndPages();
    return test::result();
}
