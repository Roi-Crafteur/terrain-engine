#pragma once

struct GLFWwindow;

namespace viewer {

// Keys 1 to 9 and 0 select what is displayed: the enumerators follow the keys.
enum class ViewMode {
    Cube,        // 1
    Sphere,      // 2
    Heightmap,   // 3
    Caves,       // 4
    Eroded,      // 5
    Cliffs,      // 6
    Sculpted,    // 7
    Streaming,   // 8
    Persistent,  // 9
    Heightfield, // 0
};

// Modes 8 and 9 show the same streamed world.
[[nodiscard]] bool streamed(ViewMode mode);

// Modes where Left Shift multiplies the camera speed: the streamed world and the caves.
[[nodiscard]] bool fastMoving(ViewMode mode);

[[nodiscard]] const char* modeName(ViewMode mode);

// Modes sharing a place of the world keep the camera when switching between them.
enum class Place {
    ChunkOrigin,
    Summit,
    CaveWorld,
    ErodedModel,
    CliffModel,
    SculptedModel,
    StreamedWorld,
};

[[nodiscard]] Place placeOf(ViewMode mode);

// The mode of the number key being pressed (GLFW_KEY_1 to GLFW_KEY_9, GLFW_KEY_0), or current.
[[nodiscard]] ViewMode readViewMode(GLFWwindow* window, ViewMode current);

} // namespace viewer
