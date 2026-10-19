#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "camera.hpp"
#include "chunk_mesh.hpp"
#include "cube_mesh.hpp"
#include "demo_chunks.hpp"
#include "model_views.hpp"
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

// Terrain chunks: Lambert lighting from a fixed sun, tinted from green to gray between the
// two altitudes of uAltitudeRange.
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
uniform vec2 uAltitudeRange;
out vec4 fragColor;
void main() {
    vec3 lightDirection = normalize(vec3(0.4, 1.0, 0.3));
    float diffuse = max(dot(normalize(vNormal), lightDirection), 0.0);
    float altitude = clamp((vHeight - uAltitudeRange.x) / (uAltitudeRange.y - uAltitudeRange.x),
                           0.0, 1.0);
    vec3 albedo = mix(vec3(0.30, 0.55, 0.25), vec3(0.65, 0.60, 0.55), altitude);
    fragColor = vec4(albedo * (0.25 + 0.75 * diffuse), 1.0);
}
)";

// Keys 1 to 7 select what is displayed.
enum class ViewMode { Cube, Sphere, Heightmap, Heightfield, Eroded, Cliffs, Sculpted };

const char* modeName(ViewMode mode) {
    switch (mode) {
    case ViewMode::Cube:
        return "cube";
    case ViewMode::Sphere:
        return "sphere chunk";
    case ViewMode::Heightmap:
        return "heightmap chunk";
    case ViewMode::Heightfield:
        return "heightfield";
    case ViewMode::Eroded:
        return "eroded region with rivers (1 unit = 50 m)";
    case ViewMode::Cliffs:
        return "cliffs region with rivers (1 unit = 12.5 m)";
    case ViewMode::Sculpted:
        return "cliffs with overhangs and arches (1 unit = 2.5 m)";
    }
    return "";
}

// Modes sharing a place of the world keep the camera when switching between them.
enum class Place { ChunkOrigin, Summit, ErodedModel, CliffModel, SculptedModel };

Place placeOf(ViewMode mode) {
    switch (mode) {
    case ViewMode::Heightfield:
        return Place::Summit;
    case ViewMode::Eroded:
        return Place::ErodedModel;
    case ViewMode::Cliffs:
        return Place::CliffModel;
    case ViewMode::Sculpted:
        return Place::SculptedModel;
    default:
        return Place::ChunkOrigin;
    }
}

struct Scene {
    viewer::ShaderProgram colorShader;
    viewer::ShaderProgram terrainShader;
    viewer::CubeMesh cube;
    viewer::ChunkMesh sphereChunk;
    viewer::ChunkMesh heightmapChunk;
};

using viewer::AltitudeRange;
using viewer::LazyViews;

// Chunk (0, 0) spans [0, 16] m on each axis: start in front of it, looking toward -Z.
constexpr viewer::Vec3 kChunkViewpoint{8.0f, 10.0f, 32.0f};
constexpr AltitudeRange kChunkAltitudes{0.0f, 16.0f};

// Looks down on a region from its viewpoint.
viewer::Camera heightfieldCamera(viewer::Vec3 viewpoint) {
    constexpr float kPitchDownDegrees = 25.0f;
    viewer::Camera camera(viewpoint);
    camera.rotate(0.0f, kPitchDownDegrees / viewer::Camera::kMouseSensitivity);
    return camera;
}

// Starting camera of a place; generates its region if needed.
viewer::Camera placeCamera(Place place, LazyViews& views) {
    switch (place) {
    case Place::Summit:
        return heightfieldCamera(viewer::heightfieldView(views.heightfield).viewpoint);
    case Place::ErodedModel:
        return heightfieldCamera(viewer::erodedView(views.eroded).viewpoint);
    case Place::CliffModel:
        return heightfieldCamera(viewer::cliffView(views.cliffs).viewpoint);
    case Place::SculptedModel:
        return heightfieldCamera(viewer::sculptedView(views.sculpted).viewpoint);
    case Place::ChunkOrigin:
        break;
    }
    return viewer::Camera(kChunkViewpoint);
}

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
    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) {
        return ViewMode::Heightfield;
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

void drawTerrain(const viewer::ShaderProgram& shader, const viewer::ChunkMesh& mesh,
                 AltitudeRange altitudes) {
    shader.setVec2("uAltitudeRange", altitudes.low, altitudes.high);
    mesh.draw();
}

// Scale model shown by a mode, if it has one and it was generated.
const viewer::ModelView* modelOf(const LazyViews& views, ViewMode mode) {
    const std::optional<viewer::ModelView>* model = nullptr;
    switch (mode) {
    case ViewMode::Eroded:
        model = &views.eroded;
        break;
    case ViewMode::Cliffs:
        model = &views.cliffs;
        break;
    case ViewMode::Sculpted:
        model = &views.sculpted;
        break;
    default:
        return nullptr;
    }
    return *model ? &**model : nullptr;
}

void renderFrame(GLFWwindow* window, const Scene& scene, const LazyViews& views,
                 ViewMode mode, const viewer::Camera& camera) {
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
    if (mode == ViewMode::Heightfield && views.heightfield) {
        drawTerrain(scene.terrainShader, views.heightfield->mesh, views.heightfield->altitudes);
        return;
    }
    if (const viewer::ModelView* model = modelOf(views, mode)) {
        drawTerrain(scene.terrainShader, model->terrain, model->altitudes);
        scene.colorShader.use();
        scene.colorShader.setMat4("uViewProjection", viewProjection);
        model->water.draw();
        return;
    }
    drawTerrain(scene.terrainShader,
                mode == ViewMode::Sphere ? scene.sphereChunk : scene.heightmapChunk,
                kChunkAltitudes);
}

void runLoop(GLFWwindow* window) {
    const Scene scene{
        {kVertexShader, kFragmentShader},
        {kTerrainVertexShader, kTerrainFragmentShader},
        {},
        viewer::ChunkMesh(viewer::buildSphereChunk()),
        viewer::ChunkMesh(viewer::buildHeightmapChunk()),
    };
    LazyViews views;
    ViewMode mode = ViewMode::Heightmap;
    viewer::Camera camera(kChunkViewpoint);
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

        // The regions are far from chunk (0, 0): the camera jumps between places.
        const ViewMode next = readViewMode(window, mode);
        if (placeOf(next) != placeOf(mode)) {
            camera = placeCamera(placeOf(next), views);
            lastTime = glfwGetTime(); // generation time is not camera movement
        }
        mode = next;

        updateTitle(window, mode, camera.position(), shownTitle);
        renderFrame(window, scene, views, mode, camera);
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
