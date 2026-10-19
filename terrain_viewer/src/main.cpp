#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "camera.hpp"
#include "chunk_mesh.hpp"
#include "cube_mesh.hpp"
#include "demo_chunks.hpp"
#include "shader.hpp"
#include "terrain/chunk_coord.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <optional>
#include <string>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr float kFovYDegrees = 70.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 1000.0f;

constexpr const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
uniform mat4 uViewProjection;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

// Terrain chunks: Lambert lighting from a fixed sun, tinted by height.
constexpr const char* kTerrainVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProjection;
out vec3 vNormal;
out float vHeight;
void main() {
    vNormal = aNormal;
    vHeight = aPosition.y;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr const char* kTerrainFragmentShader = R"(#version 330 core
in vec3 vNormal;
in float vHeight;
out vec4 fragColor;
void main() {
    vec3 lightDirection = normalize(vec3(0.4, 1.0, 0.3));
    float diffuse = max(dot(normalize(vNormal), lightDirection), 0.0);
    float altitude = clamp(vHeight / 16.0, 0.0, 1.0);
    vec3 albedo = mix(vec3(0.30, 0.55, 0.25), vec3(0.65, 0.60, 0.55), altitude);
    fragColor = vec4(albedo * (0.25 + 0.75 * diffuse), 1.0);
}
)";

// Keys 1, 2 and 3 select what is displayed.
enum class ViewMode { Cube, Sphere, Heightmap };

const char* modeName(ViewMode mode) {
    switch (mode) {
    case ViewMode::Cube:
        return "cube";
    case ViewMode::Sphere:
        return "sphere chunk";
    case ViewMode::Heightmap:
        return "heightmap chunk";
    }
    return "";
}

struct Scene {
    viewer::ShaderProgram colorShader;
    viewer::ShaderProgram terrainShader;
    viewer::CubeMesh cube;
    viewer::ChunkMesh sphereChunk;
    viewer::ChunkMesh heightmapChunk;
};

struct TitleInfo {
    ViewMode mode = ViewMode::Heightmap;
    terrain::ChunkCoord chunk;

    friend bool operator==(const TitleInfo&, const TitleInfo&) = default;
};

void onGlfwError(int code, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

GLFWwindow* createWindow() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* window =
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Terrain Engine", nullptr, nullptr);
    if (window == nullptr) {
        return nullptr;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    return window;
}

float axis(GLFWwindow* window, int positiveKey, int negativeKey) {
    float value = 0.0f;
    if (glfwGetKey(window, positiveKey) == GLFW_PRESS) {
        value += 1.0f;
    }
    if (glfwGetKey(window, negativeKey) == GLFW_PRESS) {
        value -= 1.0f;
    }
    return value;
}

// GLFW key codes name physical keys (US positions): W/A/S/D are Z/Q/S/D on AZERTY.
viewer::MoveInput readMoveInput(GLFWwindow* window) {
    return {
        .forward = axis(window, GLFW_KEY_W, GLFW_KEY_S),
        .right = axis(window, GLFW_KEY_D, GLFW_KEY_A),
        .up = axis(window, GLFW_KEY_SPACE, GLFW_KEY_LEFT_CONTROL),
    };
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
    return current;
}

void updateTitle(GLFWwindow* window, ViewMode mode, viewer::Vec3 position,
                 std::optional<TitleInfo>& shown) {
    const TitleInfo info{mode, terrain::worldToChunk(position.x, position.z)};
    if (shown == info) {
        return;
    }
    shown = info;
    const std::string title = std::format("Terrain Engine - {} - chunk ({}, {})", modeName(mode),
                                          info.chunk.x, info.chunk.z);
    glfwSetWindowTitle(window, title.c_str());
}

void renderFrame(GLFWwindow* window, const Scene& scene, ViewMode mode,
                 const viewer::Camera& camera) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    glViewport(0, 0, width, height);
    glClearColor(0.55f, 0.70f, 0.85f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const viewer::Mat4 projection =
        viewer::perspective(viewer::radians(kFovYDegrees), aspect, kNearPlane, kFarPlane);
    const viewer::Mat4 viewProjection = projection * camera.viewMatrix();
    if (mode == ViewMode::Cube) {
        scene.colorShader.use();
        scene.colorShader.setMat4("uViewProjection", viewProjection);
        scene.cube.draw();
        return;
    }
    scene.terrainShader.use();
    scene.terrainShader.setMat4("uViewProjection", viewProjection);
    (mode == ViewMode::Sphere ? scene.sphereChunk : scene.heightmapChunk).draw();
}

void runLoop(GLFWwindow* window) {
    const Scene scene{
        {kVertexShader, kFragmentShader},
        {kTerrainVertexShader, kTerrainFragmentShader},
        {},
        viewer::ChunkMesh(viewer::buildSphereChunk()),
        viewer::ChunkMesh(viewer::buildHeightmapChunk()),
    };
    ViewMode mode = ViewMode::Heightmap;
    // Chunk (0, 0) spans [0, 16] m on each axis: start in front of it, looking toward -Z.
    viewer::Camera camera({8.0f, 10.0f, 32.0f});
    std::optional<TitleInfo> shownTitle;

    double lastX = 0.0;
    double lastY = 0.0;
    glfwGetCursorPos(window, &lastX, &lastY);
    double lastTime = glfwGetTime();
    glEnable(GL_DEPTH_TEST);

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        const double now = glfwGetTime();
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        camera.rotate(static_cast<float>(x - lastX), static_cast<float>(y - lastY));
        camera.move(readMoveInput(window), static_cast<float>(now - lastTime));
        lastX = x;
        lastY = y;
        lastTime = now;
        mode = readViewMode(window, mode);

        updateTitle(window, mode, camera.position(), shownTitle);
        renderFrame(window, scene, mode, camera);
        glfwSwapBuffers(window);
    }
}

} // namespace

int main() {
    glfwSetErrorCallback(onGlfwError);
    if (glfwInit() == GLFW_FALSE) {
        return EXIT_FAILURE;
    }
    int status = EXIT_FAILURE;
    GLFWwindow* window = createWindow();
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create an OpenGL 3.3 core window\n");
    } else if (gladLoadGL(glfwGetProcAddress) == 0) {
        std::fprintf(stderr, "Failed to load OpenGL functions\n");
    } else {
        try {
            runLoop(window);
            status = EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Fatal error: %s\n", error.what());
        }
    }
    glfwTerminate();
    return status;
}
