// Where the dev map sends the player (Bridge/map_targets.hpp): heading <-> yaw (those of agent 36,
// Bridge/place_pose), the point clicked, the coordinates typed, the meters written, the view saved
// from the camera (cameraPose, placeOfView of agent 36), the places kept in memory
// (Bridge/map_places_port.hpp), and the arguments of Terrain.Map.Go.
#include "map_targets.hpp"

#include "map_reveal.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <string>

using namespace terrain;
using namespace terrain::unreal;

namespace {

void testHeadingYaw() {
    TEST_CHECK(yawOfHeading(0.0) == -90.0);  // north: -Y of Unreal
    TEST_CHECK(yawOfHeading(90.0) == 0.0);   // east: +X
    TEST_CHECK(yawOfHeading(180.0) == 90.0); // south: +Y
    TEST_CHECK(yawOfHeading(227.0) == 137.0);
    TEST_CHECK(yawOfHeading(270.0) == 180.0);
    TEST_CHECK(yawOfHeading(-450.0) == 180.0);
    TEST_CHECK(headingOfYaw(-90.0) == 0.0);
    TEST_CHECK(headingOfYaw(137.0) == 227.0);
    TEST_CHECK(headingOfYaw(-180.0) == 270.0);
    TEST_CHECK(headingOfYaw(630.0) == 0.0);
    bool round = true;
    for (double h = 0.0; h < 360.0; h += 7.5) {
        round = round && std::abs(headingOfYaw(yawOfHeading(h)) - h) < 1e-9;
    }
    TEST_CHECK(round);
}

void testClick() {
    MapView view;
    view.center = {-6185104.0, 155924.0};
    view.metersPerPixel = 2.0;
    view.width = 800.0;
    view.height = 600.0;
    const dev::Place p = clickTarget(view, {400.0 - 153.5, 300.0 + 147.0}, 405.0, -120.0);
    TEST_CHECK(p.x == -6185411.0 && p.z == 156218.0); // the lip of the 388 m fall
    TEST_CHECK(p.heading == 45.0 && p.pitch == -90.0);
    TEST_CHECK(p.eyeMode == dev::EyeMode::Ground && p.eye == kMapEyeAboveGround);
}

void testParse() {
    const auto is = [](std::string_view text, double x, double z) {
        const std::optional<WorldPoint> p = parseCoordinates(text);
        return p.has_value() && p->x == x && p->z == z;
    };
    TEST_CHECK(is("-6 185 104 ; 155 924", -6185104.0, 155924.0));
    TEST_CHECK(is("\xE2\x88\x92" "6\xE2\x80\xAF" "185\xE2\x80\xAF" "104 ; 155\xC2\xA0" "924",
                  -6185104.0, 155924.0));
    TEST_CHECK(is("-241631, -86277", -241631.0, -86277.0));
    TEST_CHECK(is("  3624471   -10006796 ", 3624471.0, -10006796.0));
    TEST_CHECK(is("7369228.5;-7088911,25", 7369228.5, -7088911.25));
    TEST_CHECK(is("+12 ; 3", 12.0, 3.0));
    for (const char* bad : {"", "12", "1 2 3", "a, b", "1;2;3", "1,2,3", "1e400, 0", "12 ; ",
                            "nan, 1"}) {
        TEST_CHECK(!parseCoordinates(bad).has_value());
    }
}

void testFormat() {
    TEST_CHECK(formatMeters(-6185104.0) == "\xE2\x88\x92" "6\xE2\x80\xAF" "185\xE2\x80\xAF" "104");
    TEST_CHECK(formatMeters(1725.26, 2) == "1\xE2\x80\xAF" "725,26");
    TEST_CHECK(formatMeters(3.28, 2) == "3,28");
    TEST_CHECK(formatMeters(-0.4) == "0");
    TEST_CHECK(formatMeters(999.0) == "999");
    TEST_CHECK(formatMeters(-0.05, 1) == "\xE2\x88\x92" "0,1");
}

// Terrain.Map.Go: its arguments as the console of Unreal splits them.
void testMapGo() {
    const MapGoCommand twoPieces = parseMapGo({"160", "0"});
    TEST_CHECK(twoPieces.point && twoPieces.point->x == 160.0 && twoPieces.point->z == 0.0);
    const MapGoCommand comma = parseMapGo({"160,", "0"});
    TEST_CHECK(comma.point && comma.point->x == 160.0 && comma.point->z == 0.0);
    const MapGoCommand negative = parseMapGo({"-241631", "-86277"});
    TEST_CHECK(negative.point && negative.point->x == -241631.0 && negative.point->z == -86277.0);
    const MapGoCommand grouped = parseMapGo({"-6", "185", "104", ";", "155", "924"});
    TEST_CHECK(grouped.point && grouped.point->x == -6185104.0 && grouped.point->z == 155924.0);
    // Digits grouped by spaces only with a separator: nothing guessed.
    const MapGoCommand noSeparator = parseMapGo({"-6", "185", "104", "155", "924"});
    TEST_CHECK(!noSeparator.point && noSeparator.place.empty());
    TEST_CHECK(noSeparator.error.find("usage: Terrain.Map.Go x z") != std::string::npos);
    const MapGoCommand three = parseMapGo({"1", "2", "3"});
    TEST_CHECK(!three.point && three.place.empty() && !three.error.empty());
    const MapGoCommand alone = parseMapGo({"160"}); // a number, not a place
    TEST_CHECK(!alone.point && alone.place.empty() && !alone.error.empty());
    const MapGoCommand place = parseMapGo({"k2_paroi"});
    TEST_CHECK(!place.point && place.place == "k2_paroi" && place.error.empty());
    const MapGoCommand words = parseMapGo({"deux", "mots"});
    TEST_CHECK(!words.point && words.place.empty() && !words.error.empty());
    const MapGoCommand none = parseMapGo({});
    TEST_CHECK(!none.point && none.place.empty() && none.error.find("usage") != std::string::npos);
}

// "Add this place": the view of the camera as Terrain.Save writes it (agent 36), kept by a source.
void testCameraAndPlaces() {
    const ViewPose pose =
        cameraPose({-6185104.0 + WorldSize{}.x, 182.03, 155924.0}, 30.0, 137.0);
    const dev::Place p = placeOfView("chute388_bassin", "la chute de 388 m", pose, true, WorldSize{});
    TEST_CHECK(p.x == -6185104.0 && p.z == 155924.0 && p.heading == 227.0 && p.pitch == 30.0);
    TEST_CHECK(p.eye == 182.03 && p.eyeMode == dev::EyeMode::Altitude && p.planet);
    MemoryPlaces places;
    TEST_CHECK(places.add(p) && places.places().size() == 1);
    dev::Place moved = p;
    moved.heading = 90.0;
    moved.note.clear(); // put keeps the note when none is given
    TEST_CHECK(places.add(moved) && places.places().size() == 1 &&
               places.places().front().heading == 90.0 &&
               places.places().front().note == "la chute de 388 m");
    dev::Place unnamed;
    TEST_CHECK(!places.add(unnamed) && !places.error().empty());
    const RevealEverything all;
    TEST_CHECK(all.revealedAt(1.0, 2.0) == 1.0f && all.wholeTile({3, 1, 1}));
}

} // namespace

int main() {
    testHeadingYaw();
    testClick();
    testParse();
    testFormat();
    testCameraAndPlaces();
    testMapGo();
    return test::result();
}
