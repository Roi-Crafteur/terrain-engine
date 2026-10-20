#include "view_modes.hpp"

#include <GLFW/glfw3.h>

namespace viewer {

bool streamed(ViewMode mode) {
    return mode == ViewMode::Streaming || mode == ViewMode::Persistent;
}

bool fastMoving(ViewMode mode) {
    return streamed(mode) || mode == ViewMode::Caves;
}

const char* modeName(ViewMode mode) {
    switch (mode) {
    case ViewMode::Cube:
        return "cube";
    case ViewMode::Sphere:
        return "sphere chunk";
    case ViewMode::Heightmap:
        return "heightmap chunk";
    case ViewMode::Caves:
        return "caves (fly underground: the rock is seen through, the caves are not; Shift: fast)";
    case ViewMode::Eroded:
        return "eroded region with rivers (1 unit = 50 m)";
    case ViewMode::Cliffs:
        return "cliffs region with rivers (1 unit = 12.5 m)";
    case ViewMode::Sculpted:
        return "cliffs with overhangs and arches (1 unit = 2.5 m)";
    case ViewMode::Streaming:
        return "streaming + LOD (tint: LOD 0 green, 1 yellow, 2 orange, 3 purple; Shift: fast)";
    case ViewMode::Persistent:
        return "save and reload (LMB dig, RMB build, P tree, O uproot, F5 save, F9 reload)";
    case ViewMode::Heightfield:
        return "heightfield";
    }
    return "";
}

Place placeOf(ViewMode mode) {
    switch (mode) {
    case ViewMode::Heightfield:
        return Place::Summit;
    case ViewMode::Caves:
        return Place::CaveWorld;
    case ViewMode::Eroded:
        return Place::ErodedModel;
    case ViewMode::Cliffs:
        return Place::CliffModel;
    case ViewMode::Sculpted:
        return Place::SculptedModel;
    case ViewMode::Streaming:
    case ViewMode::Persistent:
        return Place::StreamedWorld;
    default:
        return Place::ChunkOrigin;
    }
}

ViewMode readViewMode(GLFWwindow* window, ViewMode current) {
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
        return ViewMode::Cube;
    }
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        return ViewMode::Sphere;
    }
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) {
        return ViewMode::Heightmap;
    }
    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) {
        return ViewMode::Caves;
    }
    if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS) {
        return ViewMode::Eroded;
    }
    if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS) {
        return ViewMode::Cliffs;
    }
    if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS) {
        return ViewMode::Sculpted;
    }
    if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS) {
        return ViewMode::Streaming;
    }
    if (glfwGetKey(window, GLFW_KEY_9) == GLFW_PRESS) {
        return ViewMode::Persistent;
    }
    if (glfwGetKey(window, GLFW_KEY_0) == GLFW_PRESS) {
        return ViewMode::Heightfield;
    }
    return current;
}

} // namespace viewer
