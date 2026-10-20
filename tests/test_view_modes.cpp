// The view modes of the viewer, without a window: the places and speeds of the cave modes, the
// names, and how key 5 chooses between the cave regions and the eroded region on its press only.
#include "view_modes.hpp"

#include "test_assert.hpp"

#include <array>
#include <cstddef>
#include <cstring>

namespace {

using viewer::ViewMode;

constexpr std::array<ViewMode, 11> kAllModes = {
    ViewMode::Cube,      ViewMode::Sphere,     ViewMode::Heightmap,   ViewMode::Caves,
    ViewMode::CaveRegions, ViewMode::Eroded,   ViewMode::Cliffs,      ViewMode::Sculpted,
    ViewMode::Streaming, ViewMode::Persistent, ViewMode::Heightfield,
};

// Key 5 acts when it goes down, not while it is held, and the state follows the key.
void testFivePressed() {
    viewer::ModeKeys keys;
    constexpr std::array<bool, 7> down = {false, true, true, true, false, false, true};
    constexpr std::array<bool, 7> expected = {false, true, false, false, false, false, true};
    for (std::size_t i = 0; i < down.size(); ++i) {
        TEST_CHECK(viewer::fivePressed(keys, down[i]) == expected[i]);
        TEST_CHECK(keys.fiveDown == down[i]);
    }
}

// Shift decides when 5 goes down: without it the cave regions, with it the eroded region.
void testFiveMode() {
    TEST_CHECK(viewer::fiveMode(false) == ViewMode::CaveRegions);
    TEST_CHECK(viewer::fiveMode(true) == ViewMode::Eroded);

    // A held 5 gives one press (the choice between the two modes is made then), however long it
    // is held and whatever Shift does meanwhile; releasing it and pressing it again is a new one.
    viewer::ModeKeys keys;
    TEST_CHECK(viewer::fivePressed(keys, true));
    for (int frame = 0; frame < 5; ++frame) {
        TEST_CHECK(!viewer::fivePressed(keys, true));
    }
    TEST_CHECK(!viewer::fivePressed(keys, false));
    TEST_CHECK(viewer::fivePressed(keys, true));
}

// The cave modes are two worlds, each with its own spawn; both fly fast and only they are
// underground.
void testCaveModes() {
    TEST_CHECK(viewer::placeOf(ViewMode::Caves) != viewer::placeOf(ViewMode::CaveRegions));
    TEST_CHECK(viewer::placeOf(ViewMode::CaveRegions) != viewer::placeOf(ViewMode::Eroded));
    TEST_CHECK(viewer::placeOf(ViewMode::CaveRegions) == viewer::Place::CaveRegionWorld);
    TEST_CHECK(viewer::placeOf(ViewMode::Caves) == viewer::Place::CaveWorld);
    TEST_CHECK(viewer::fastMoving(ViewMode::Caves) && viewer::fastMoving(ViewMode::CaveRegions));
    TEST_CHECK(!viewer::fastMoving(ViewMode::Eroded));
    TEST_CHECK(viewer::fastMoving(ViewMode::Streaming) && viewer::fastMoving(ViewMode::Persistent));
    int underground = 0;
    for (const ViewMode mode : kAllModes) {
        underground += viewer::underground(mode) ? 1 : 0;
    }
    TEST_CHECK(underground == 2);
    TEST_CHECK(viewer::underground(ViewMode::Caves) && viewer::underground(ViewMode::CaveRegions));
}

// Every mode has its own name, and the new ones tell their keys.
void testNames() {
    bool distinct = true;
    for (const ViewMode a : kAllModes) {
        distinct = distinct && std::strlen(viewer::modeName(a)) > 0;
        for (const ViewMode b : kAllModes) {
            const bool same = std::strcmp(viewer::modeName(a), viewer::modeName(b)) == 0;
            distinct = distinct && (a == b || !same);
        }
    }
    TEST_CHECK(distinct);
    TEST_CHECK(std::strstr(viewer::modeName(ViewMode::CaveRegions), "cave regions") != nullptr);
    TEST_CHECK(std::strstr(viewer::modeName(ViewMode::Eroded), "Shift+5") != nullptr);
}

} // namespace

int main() {
    testFivePressed();
    testFiveMode();
    testCaveModes();
    testNames();
    return test::result();
}
