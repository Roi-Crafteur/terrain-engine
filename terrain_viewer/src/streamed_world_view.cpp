#include "streamed_world_view.hpp"

#include "heightfield_region.hpp"
#include "terrain/chunk_coord.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/storage/modifications.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <thread>
#include <utility>
#include <vector>

namespace viewer {

namespace {

// Terrain lit as in the other modes, tinted by the LOD of its tile: uLodTint holds the LOD and
// the weight of its color (0 for no tint).
constexpr const char* kVertexShader = R"(#version 330 core
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

constexpr const char* kFragmentShader = R"(#version 330 core
in vec3 vNormal;
in float vHeight;
uniform vec2 uAltitudeRange;
uniform vec2 uLodTint;
out vec4 fragColor;
const vec3 kLodColors[4] = vec3[4](vec3(0.20, 0.80, 0.30), vec3(0.95, 0.85, 0.20),
                                   vec3(0.95, 0.45, 0.15), vec3(0.70, 0.30, 0.90));
void main() {
    vec3 lightDirection = normalize(vec3(0.4, 1.0, 0.3));
    float diffuse = max(dot(normalize(vNormal), lightDirection), 0.0);
    float altitude = clamp((vHeight - uAltitudeRange.x) / (uAltitudeRange.y - uAltitudeRange.x),
                           0.0, 1.0);
    vec3 albedo = mix(vec3(0.30, 0.55, 0.25), vec3(0.65, 0.60, 0.55), altitude);
    albedo = mix(albedo, kLodColors[clamp(int(uLodTint.x + 0.5), 0, 3)], uLodTint.y);
    fragColor = vec4(albedo * (0.25 + 0.75 * diffuse), 1.0);
}
)";

constexpr float kLodTint = 0.35f;

// Tiles uploaded per frame, worker threads at most.
constexpr std::size_t kUploadsPerFrame = 8;
constexpr int kMaxWorkers = 4;

// Aiming: the ray is walked in steps of kAimStep meters up to kReachMeters; a plant is uprooted
// within kUprootRadius meters of the aimed point.
constexpr float kReachMeters = 200.0f;
constexpr float kAimStep = 0.25f;
constexpr double kUprootRadius = 3.0;

// The sea is a square of side 2 kSeaHalfSize meters, kSeaDrop meters under sea level.
constexpr float kSeaHalfSize = 4000.0f;
constexpr float kSeaDrop = 0.05f;
constexpr Vec3 kSeaColor{0.10f, 0.30f, 0.60f};

// The camera starts south of the summit, above the ground around it (probed every
// kSpawnProbe meters).
constexpr float kSpawnClearance = 40.0f;
constexpr double kSpawnSetback = 200.0;
constexpr double kSpawnArea = 256.0;
constexpr double kSpawnProbe = 16.0;

struct PlantLook {
    Vec3 color;
    float height; // meters, at scale 1
    float width;
};

PlantLook lookOf(terrain::PlantType type) {
    switch (type) {
    case terrain::PlantType::Tree:
        return {{0.10f, 0.35f, 0.12f}, 7.0f, 3.0f};
    case terrain::PlantType::Bush:
        return {{0.20f, 0.45f, 0.15f}, 1.5f, 1.5f};
    case terrain::PlantType::Flower:
        return {{0.90f, 0.40f, 0.70f}, 0.5f, 0.4f};
    case terrain::PlantType::TallGrass:
        return {{0.55f, 0.70f, 0.25f}, 0.9f, 0.6f};
    case terrain::PlantType::Cactus:
        return {{0.35f, 0.55f, 0.20f}, 3.0f, 0.8f};
    case terrain::PlantType::Boulder:
        return {{0.50f, 0.48f, 0.45f}, 1.2f, 1.5f};
    case terrain::PlantType::Seaweed:
        return {{0.10f, 0.45f, 0.40f}, 1.5f, 0.5f};
    }
    return {{1.0f, 0.0f, 1.0f}, 1.0f, 1.0f};
}

void appendQuad(std::vector<ColoredVertex>& out, const std::array<Vec3, 4>& corners, Vec3 color) {
    for (const std::size_t i : {0U, 1U, 2U, 0U, 2U, 3U}) {
        out.push_back({corners[i], color});
    }
}

// Marker of a plant: two crossed vertical quads, turned by its rotation.
void appendPlant(std::vector<ColoredVertex>& out, const terrain::Plant& plant) {
    const PlantLook look = lookOf(plant.type);
    const Vec3 base{static_cast<float>(plant.x), plant.y, static_cast<float>(plant.z)};
    const Vec3 up{0.0f, look.height * plant.scale, 0.0f};
    const float half = look.width * plant.scale / 2.0f;
    for (const float angle : {plant.rotation, plant.rotation + radians(90.0f)}) {
        const Vec3 side{std::cos(angle) * half, 0.0f, std::sin(angle) * half};
        appendQuad(out, {base - side, base + side, base + side + up, base - side + up},
                   look.color);
    }
}

terrain::Mesh worldMesh(const terrain::TileData& data) {
    terrain::Mesh mesh = data.mesh;
    for (terrain::Vec3f& v : mesh.vertices) {
        v = {static_cast<float>(data.originX + v.x), static_cast<float>(data.originY + v.y),
             static_cast<float>(data.originZ + v.z)};
    }
    return mesh;
}

// A small cross at the center of the screen, in normalized device coordinates.
std::vector<ColoredVertex> crosshairQuads() {
    constexpr Vec3 kWhite{1.0f, 1.0f, 1.0f};
    constexpr float kLong = 0.02f;
    constexpr float kThin = 0.002f;
    std::vector<ColoredVertex> cross;
    appendQuad(cross, {Vec3{-kLong, -kThin, 0.0f}, Vec3{kLong, -kThin, 0.0f},
                       Vec3{kLong, kThin, 0.0f}, Vec3{-kLong, kThin, 0.0f}}, kWhite);
    appendQuad(cross, {Vec3{-kThin, -1.8f * kLong, 0.0f}, Vec3{kThin, -1.8f * kLong, 0.0f},
                       Vec3{kThin, 1.8f * kLong, 0.0f}, Vec3{-kThin, 1.8f * kLong, 0.0f}}, kWhite);
    return cross;
}

terrain::StreamParams streamParams() {
    terrain::StreamParams params; // default LOD rings: 32 chunks of view
    const auto cores = static_cast<int>(std::thread::hardware_concurrency());
    params.workerThreads = std::clamp(cores - 1, 1, kMaxWorkers);
    params.maxLoadsPerUpdate = kUploadsPerFrame;
    return params;
}

// Nearest lattice point: the block containing p.
terrain::BlockCoord latticePoint(Vec3 p) {
    const auto round = [](float v) { return static_cast<std::int32_t>(std::lround(v)); };
    return {round(p.x), round(p.y), round(p.z)};
}

std::string describe(terrain::BlockCoord b) {
    return std::format("({}, {}, {})", b.x, b.y, b.z);
}

} // namespace

std::vector<ColoredVertex> seaQuad(Vec3 center) {
    const float y = terrain::kSeaLevel - kSeaDrop;
    const float h = kSeaHalfSize;
    std::vector<ColoredVertex> sea;
    appendQuad(sea,
               {Vec3{center.x - h, y, center.z - h}, Vec3{center.x + h, y, center.z - h},
                Vec3{center.x + h, y, center.z + h}, Vec3{center.x - h, y, center.z + h}},
               kSeaColor);
    return sea;
}

// South of a summit, kSpawnClearance meters above the highest ground within kSpawnArea meters,
// so that nothing near blocks the view of the LOD rings.
Vec3 streamedSpawn(const terrain::Heightfield& field) {
    const GroundPoint summit = findSummit(field);
    const double x = summit.x;
    const double z = summit.z + kSpawnSetback;
    float top = field.heightAt(x, z);
    for (double dz = -kSpawnArea; dz <= kSpawnArea; dz += kSpawnProbe) {
        for (double dx = -kSpawnArea; dx <= kSpawnArea; dx += kSpawnProbe) {
            top = std::max(top, field.heightAt(x + dx, z + dz));
        }
    }
    return {static_cast<float>(x), top + kSpawnClearance, static_cast<float>(z)};
}

void applyChanges(TileViews& views, const terrain::StreamManager& manager,
                  const terrain::StreamChanges& changes) {
    for (const terrain::TileKey& key : changes.unloaded) {
        views.erase(key);
    }
    for (const terrain::TileKey& key : changes.loaded) {
        views[key] = std::make_unique<TileView>(*manager.find(key));
    }
}

std::string tileStatus(const TileViews& views, const terrain::StreamManager& manager) {
    std::array<int, terrain::kLodCount> counts{};
    for (const auto& [key, view] : views) {
        ++counts[static_cast<std::size_t>(view->lod)];
    }
    return std::format("tiles LOD0 {} LOD1 {} LOD2 {} LOD3 {}, {} pending", counts[0], counts[1],
                       counts[2], counts[3], manager.pendingCount());
}

// What the camera aims at: the first lattice point in rock along the ray, the lattice point in
// the air just before it, and the point of the ray where it entered the rock.
struct StreamedWorldView::Aim {
    terrain::BlockCoord solid;
    terrain::BlockCoord air;
    Vec3 hit;
};

TileView::TileView(const terrain::TileData& data)
    : terrain(worldMesh(data)), lod(data.request.key.lod) {
    if (!data.plants.empty()) {
        std::vector<ColoredVertex> markers;
        for (const terrain::Plant& plant : data.plants) {
            appendPlant(markers, plant);
        }
        plants.emplace(markers);
    }
}

StreamedWorldView::StreamedWorldView()
    : tiles_(terrain::kDefaultWorldSeed), manager_(terrain::tileGenerator(tiles_), streamParams()),
      terrainShader_(kVertexShader, kFragmentShader),
      crosshair_(std::make_unique<ColoredMesh>(crosshairQuads())),
      spawn_(streamedSpawn(tiles_.heightfield())) {
    std::fprintf(stderr, "Streaming the world around the camera (seed %llu, %d worker threads)\n",
                 static_cast<unsigned long long>(tiles_.seed()), manager_.params().workerThreads);
}

StreamedWorldView::~StreamedWorldView() = default;

void StreamedWorldView::update(Vec3 camera) {
    const terrain::StreamChanges changes = manager_.update(camera.x, camera.z);
    applyChanges(views_, manager_, changes);
    const float dx = camera.x - seaCenter_.x;
    const float dz = camera.z - seaCenter_.z;
    if (!sea_ || std::hypot(dx, dz) > kSeaRecenter) {
        seaCenter_ = {camera.x, 0.0f, camera.z};
        sea_ = std::make_unique<ColoredMesh>(seaQuad(seaCenter_));
    }
}

void StreamedWorldView::draw(const Mat4& viewProjection, const ShaderProgram& colorShader,
                             bool tintLods, bool crosshair) const {
    terrainShader_.use();
    terrainShader_.setMat4("uViewProjection", viewProjection);
    terrainShader_.setVec2("uAltitudeRange", kStreamedLowAltitude, kStreamedHighAltitude);
    for (const auto& [key, view] : views_) {
        terrainShader_.setVec2("uLodTint", static_cast<float>(view->lod),
                               tintLods ? kLodTint : 0.0f);
        view->terrain.draw();
    }
    colorShader.use();
    colorShader.setMat4("uViewProjection", viewProjection);
    for (const auto& [key, view] : views_) {
        if (view->plants) {
            view->plants->draw();
        }
    }
    if (sea_) {
        sea_->draw();
    }
    if (crosshair) {
        colorShader.setMat4("uViewProjection", Mat4::identity());
        glDisable(GL_DEPTH_TEST);
        crosshair_->draw();
        glEnable(GL_DEPTH_TEST);
    }
}

void StreamedWorldView::handleEditInput(GLFWwindow* window, Vec3 eye, Vec3 direction) {
    const std::array<bool, 6> down = {
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS,
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS,
    };
    std::array<bool, 6> pressed{};
    for (std::size_t i = 0; i < down.size(); ++i) {
        pressed[i] = down[i] && !held_[i];
    }
    held_ = down;
    if (pressed[4]) {
        save();
    }
    if (pressed[5]) {
        reload();
    }
    if (!pressed[0] && !pressed[1] && !pressed[2] && !pressed[3]) {
        return;
    }
    const std::optional<Aim> target = aim(eye, direction);
    if (!target) {
        message_ = std::format("no ground within {:.0f} m", kReachMeters);
    } else if (pressed[0]) {
        digBlock(*target);
    } else if (pressed[1]) {
        buildBlock(*target);
    } else if (pressed[2]) {
        plantTree(*target);
    } else {
        uprootPlant(*target);
    }
}

void StreamedWorldView::reloadIfSaved() {
    if (std::filesystem::exists(kModificationFile)) {
        reload();
    }
}

std::string StreamedWorldView::status(bool editing) const {
    std::string text = tileStatus(views_, manager_);
    if (editing) {
        const terrain::ModificationStore edits = tiles_.modifications();
        text += std::format(" | {} blocks, {} plants edited", edits.blockCount(),
                            edits.removedPlantCount() + edits.addedPlantCount());
        if (!message_.empty()) {
            text += " | " + message_;
        }
    }
    return text;
}

std::optional<StreamedWorldView::Aim> StreamedWorldView::aim(Vec3 eye, Vec3 direction) const {
    const Vec3 step = normalize(direction) * kAimStep;
    terrain::BlockCoord previous = latticePoint(eye);
    Vec3 p = eye;
    for (float travelled = 0.0f; travelled <= kReachMeters; travelled += kAimStep) {
        const terrain::BlockCoord point = latticePoint(p);
        if (tiles_.densityAt(point.x, point.y, point.z) < 0.0f) {
            return Aim{point, previous, p};
        }
        previous = point;
        p = p + step;
    }
    return std::nullopt;
}

// Digging a built block gives it back to the generated terrain, otherwise removes the rock.
void StreamedWorldView::digBlock(const Aim& target) {
    if (tiles_.blockAt(target.solid) == terrain::BlockEdit::Added) {
        invalidate(tiles_.clearBlock(target.solid));
    } else {
        invalidate(tiles_.setBlock(target.solid, terrain::BlockEdit::Removed));
    }
    message_ = "dug block " + describe(target.solid);
}

// Building in a dug block gives it back to the generated terrain, otherwise adds rock.
void StreamedWorldView::buildBlock(const Aim& target) {
    if (tiles_.blockAt(target.air) == terrain::BlockEdit::Removed) {
        invalidate(tiles_.clearBlock(target.air));
    } else {
        invalidate(tiles_.setBlock(target.air, terrain::BlockEdit::Added));
    }
    message_ = "built block " + describe(target.air);
}

void StreamedWorldView::plantTree(const Aim& target) {
    const terrain::Plant tree{terrain::PlantType::Tree, target.hit.x, target.hit.z, target.hit.y,
                              1.0f, 0.0f};
    invalidate(tiles_.addPlant(tree));
    message_ = std::format("planted a tree at ({:.1f}, {:.1f})", tree.x, tree.z);
}

// The plant of the loaded LOD 0 tiles nearest to the aimed point, within kUprootRadius.
void StreamedWorldView::uprootPlant(const Aim& target) {
    const terrain::ChunkCoord center = terrain::worldToChunk(target.hit.x, target.hit.z);
    std::optional<terrain::Plant> nearest;
    double best = kUprootRadius;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const terrain::TileData* data =
                manager_.find({{center.x + dx, center.z + dz}, terrain::kPlantLod});
            if (data == nullptr) {
                continue;
            }
            for (const terrain::Plant& plant : data->plants) {
                const double d = std::hypot(plant.x - target.hit.x, plant.z - target.hit.z);
                if (d <= best) {
                    best = d;
                    nearest = plant;
                }
            }
        }
    }
    if (!nearest) {
        message_ = std::format("no plant within {:.0f} m", kUprootRadius);
        return;
    }
    invalidate(tiles_.removePlant(*nearest));
    message_ = std::format("uprooted a {}", terrain::plantName(nearest->type));
}

void StreamedWorldView::save() {
    try {
        terrain::saveModifications(kModificationFile, tiles_.modifications(), tiles_.seed());
        message_ = std::format("saved to {}", kModificationFile);
    } catch (const std::exception& error) {
        message_ = std::format("save failed: {}", error.what());
    }
    std::fprintf(stderr, "%s\n", message_.c_str());
}

// Replaces the modifications by the file's; the tiles they touch are generated again.
void StreamedWorldView::reload() {
    try {
        terrain::SavedModifications saved = terrain::loadModifications(kModificationFile);
        if (saved.seed != tiles_.seed()) {
            message_ = std::format("{} belongs to another world (seed {})", kModificationFile,
                                   saved.seed);
        } else {
            const std::size_t blocks = saved.store.blockCount();
            const std::size_t plants =
                saved.store.removedPlantCount() + saved.store.addedPlantCount();
            invalidate(tiles_.replaceModifications(std::move(saved.store)));
            message_ = std::format("reloaded {}: {} blocks, {} plants", kModificationFile, blocks,
                                   plants);
        }
    } catch (const std::exception& error) {
        message_ = std::format("reload failed: {}", error.what());
    }
    std::fprintf(stderr, "%s\n", message_.c_str());
}

void StreamedWorldView::invalidate(const std::vector<terrain::ChunkCoord>& chunks) {
    manager_.invalidate(chunks);
}

} // namespace viewer
