#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "camera.hpp"
#include "cube_mesh.hpp"
#include "shader.hpp"
#include "terrain/chunk_coord.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <optional>

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

void updateTitle(GLFWwindow* window, viewer::Vec3 position,
                 std::optional<terrain::ChunkCoord>& shownChunk) {
    const terrain::ChunkCoord chunk = terrain::worldToChunk(position.x, position.z);
    if (shownChunk == chunk) {
        return;
    }
    shownChunk = chunk;
    const std::string title = std::format("Terrain Engine - chunk ({}, {})", chunk.x, chunk.z);
    glfwSetWindowTitle(window, title.c_str());
}

void renderFrame(GLFWwindow* window, const viewer::ShaderProgram& shader,
                 const viewer::CubeMesh& cube, const viewer::Camera& camera) {
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
    shader.use();
    shader.setMat4("uViewProjection", projection * camera.viewMatrix());
    cube.draw();
}

void runLoop(GLFWwindow* window) {
    const viewer::ShaderProgram shader(kVertexShader, kFragmentShader);
    const viewer::CubeMesh cube;
    viewer::Camera camera({0.0f, 0.0f, 3.0f});
    std::optional<terrain::ChunkCoord> shownChunk;

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

        updateTitle(window, camera.position(), shownChunk);
        renderFrame(window, shader, cube, camera);
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
