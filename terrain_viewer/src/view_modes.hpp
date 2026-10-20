#pragma once

struct GLFWwindow;

namespace viewer {

// Keys 1 to 9 and 0 select what is displayed: the enumerators follow the keys, except that key 5
// holds two modes (CaveRegions, and Eroded with Shift: see ModeKeys).
enum class ViewMode {
    Cube,        // 1
    Sphere,      // 2
    Heightmap,   // 3
    Caves,       // 4
    CaveRegions, // 5
    Eroded,      // Shift+5
    Cliffs,      // 6
    Sculpted,    // 7
    Streaming,   // 8
    Persistent,  // 9
    Heightfield, // 0
};

// Modes 8 and 9 show the same streamed world.
[[nodiscard]] bool streamed(ViewMode mode);

// Modes 4 and 5 fly in the caves, each in its own world (without and with cave regions).
[[nodiscard]] bool underground(ViewMode mode);

// Modes where Left Shift multiplies the camera speed: the streamed world and the caves.
[[nodiscard]] bool fastMoving(ViewMode mode);

[[nodiscard]] const char* modeName(ViewMode mode);

// Modes sharing a place of the world keep the camera when switching between them.
enum class Place {
    ChunkOrigin,
    Summit,
    CaveWorld,
    CaveRegionWorld,
    ErodedModel,
    CliffModel,
    SculptedModel,
    StreamedWorld,
};

[[nodiscard]] Place placeOf(ViewMode mode);

// State that key 5 needs between two frames. Left Shift is held to fly fast in the caves, so
// the choice between the two modes of key 5 is made on its press only: never when Shift goes
// down or up, and never again while 5 is held.
struct ModeKeys {
    bool fiveDown = false; // key 5 was down at the previous call
};

// True when key 5 is down now and was not at the previous call (a press, not a hold). Records
// the state of the key.
[[nodiscard]] bool fivePressed(ModeKeys& keys, bool fiveDown);

// The mode that a press of key 5 selects: Eroded with Shift held at that moment, else
// CaveRegions.
[[nodiscard]] ViewMode fiveMode(bool shift);

// The mode of the number key being pressed (GLFW_KEY_1 to GLFW_KEY_9, GLFW_KEY_0), or current.
// Call it every frame: it follows key 5 (keys) to see its presses. Key 5 selects CaveRegions, or
// Eroded when Left or Right Shift is held as it goes down.
[[nodiscard]] ViewMode readViewMode(GLFWwindow* window, ViewMode current, ModeKeys& keys);

} // namespace viewer
