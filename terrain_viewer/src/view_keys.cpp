#include "view_modes.hpp"

#include <GLFW/glfw3.h>

namespace viewer {

ViewMode readViewMode(GLFWwindow* window, ViewMode current, ModeKeys& keys) {
    const auto down = [window](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
    // Key 5 is followed on every call, even when a key before it wins: its press is seen once.
    const bool five = fivePressed(keys, down(GLFW_KEY_5));
    if (down(GLFW_KEY_1)) {
        return ViewMode::Cube;
    }
    if (down(GLFW_KEY_2)) {
        return ViewMode::Sphere;
    }
    if (down(GLFW_KEY_3)) {
        return ViewMode::Heightmap;
    }
    if (down(GLFW_KEY_4)) {
        return ViewMode::Caves;
    }
    if (five) {
        return fiveMode(down(GLFW_KEY_LEFT_SHIFT) || down(GLFW_KEY_RIGHT_SHIFT));
    }
    if (down(GLFW_KEY_6)) {
        return ViewMode::Cliffs;
    }
    if (down(GLFW_KEY_7)) {
        return ViewMode::Sculpted;
    }
    if (down(GLFW_KEY_8)) {
        return ViewMode::Streaming;
    }
    if (down(GLFW_KEY_9)) {
        return ViewMode::Persistent;
    }
    if (down(GLFW_KEY_0)) {
        return ViewMode::Heightfield;
    }
    return current;
}

} // namespace viewer
