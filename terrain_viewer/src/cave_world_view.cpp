#include "cave_world_view.hpp"

#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <vector>

namespace viewer {

namespace {

// Terrain lit as in the streamed world (sun, tint from green to gray by altitude), plus a
// headlight at uEye along uViewDirection: a wide soft cone, fading with the distance, so that
// the ceilings and walls of the caves are lit.
constexpr const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec3 vPosition;
void main() {
    vNormal = aNormal;
    vPosition = aPosition;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 330 core
in vec3 vNormal;
in vec3 vPosition;
uniform vec2 uAltitudeRange;
uniform vec3 uEye;
uniform vec3 uViewDirection;
out vec4 fragColor;
const float kHeadlightRange = 40.0;
const float kHeadlightStrength = 0.9;
void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDirection = normalize(vec3(0.4, 1.0, 0.3));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float altitude = clamp((vPosition.y - uAltitudeRange.x) / (uAltitudeRange.y - uAltitudeRange.x),
                           0.0, 1.0);
    vec3 albedo = mix(vec3(0.30, 0.55, 0.25), vec3(0.65, 0.60, 0.55), altitude);

    vec3 toEye = uEye - vPosition;
    float distance = max(length(toEye), 0.001);
    float facing = max(dot(normal, toEye / distance), 0.0);
    float cone = smoothstep(0.2, 0.9, dot(-toEye / distance, normalize(uViewDirection)));
    float range = distance / kHeadlightRange;
    float headlight = kHeadlightStrength * facing * cone / (1.0 + range * range);
    fragColor = vec4(albedo * (0.25 + 0.75 * diffuse + headlight), 1.0);
}
)";

// Tiles uploaded per frame, worker threads.
constexpr std::size_t kUploadsPerFrame = 8;
constexpr int kWorkers = 2;

// The entrances are searched around the spawn of the streamed world, in squares of half-side
// kSearchStart meters, doubled up to kSearchMax.
constexpr double kSearchStart = 256.0;
constexpr double kSearchMax = 4096.0;

// The camera starts kSpawnSetback meters south of the top of an entrance and kSpawnHeight meters
// above it, at least kSpawnClearance meters above the ground there.
constexpr double kSpawnSetback = 40.0;
constexpr double kSpawnHeight = 25.0;
constexpr double kSpawnClearance = 10.0;

terrain::StreamParams streamParams() {
    terrain::StreamParams params; // default LOD rings: 32 chunks of view
    params.workerThreads = kWorkers;
    params.maxLoadsPerUpdate = kUploadsPerFrame;
    return params;
}

// The entrance whose top is nearest to (x, z), within kSearchMax meters.
std::optional<terrain::CaveEntrance> nearestEntrance(const terrain::TerrainTiles& tiles, double x,
                                                     double z) {
    const terrain::CaveField* caves = tiles.caves();
    if (caves == nullptr) {
        return std::nullopt;
    }
    for (double radius = kSearchStart; radius <= kSearchMax; radius *= 2.0) {
        const std::vector<terrain::CaveEntrance> found = caves->entrancesNear(
            x - radius, z - radius, x + radius, z + radius, tiles.caveGround());
        std::optional<terrain::CaveEntrance> nearest;
        double best = radius; // a nearer entrance than the square's half-side is always found
        for (const terrain::CaveEntrance& entrance : found) {
            const double distance = std::hypot(entrance.topX - x, entrance.topZ - z);
            if (distance <= best) {
                best = distance;
                nearest = entrance;
            }
        }
        if (nearest) {
            return nearest;
        }
    }
    return std::nullopt;
}

CaveSpawn spawnPoint(const terrain::TerrainTiles& tiles) {
    const Vec3 start = streamedSpawn(tiles.heightfield());
    const std::optional<terrain::CaveEntrance> entrance =
        nearestEntrance(tiles, start.x, start.z);
    if (!entrance) {
        std::fprintf(stderr, "No cave entrance within %.0f m: starting as the streamed world\n",
                     kSearchMax);
        return {start, kStreamedPitchDegrees};
    }
    const double x = entrance->topX;
    const double z = entrance->topZ + kSpawnSetback;
    const double ground = tiles.heightfield().heightAt(x, z);
    const double y = std::max(entrance->topY + kSpawnHeight, ground + kSpawnClearance);
    const double pitch = std::atan2(y - entrance->topY, kSpawnSetback);
    std::fprintf(stderr, "Cave entrance at (%.0f, %.0f, %.0f), chamber %.0f m deep\n",
                 entrance->topX, entrance->topY, entrance->topZ,
                 entrance->topY - entrance->bottomY);
    return {{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
            static_cast<float>(pitch * 180.0 / std::numbers::pi)};
}

} // namespace

CaveWorldView::CaveWorldView()
    : tiles_(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, terrain::EcologyParams{},
             terrain::CaveParams{}),
      manager_(terrain::tileGenerator(tiles_), streamParams()),
      terrainShader_(kVertexShader, kFragmentShader), spawn_(spawnPoint(tiles_)) {
    std::fprintf(stderr,
                 "Streaming the world with its caves around the camera (seed %llu, %d worker "
                 "threads)\n",
                 static_cast<unsigned long long>(tiles_.seed()), manager_.params().workerThreads);
}

CaveWorldView::~CaveWorldView() = default;

void CaveWorldView::update(Vec3 camera) {
    const terrain::StreamChanges changes = manager_.update(camera.x, camera.z);
    applyChanges(views_, manager_, changes);
    const float dx = camera.x - seaCenter_.x;
    const float dz = camera.z - seaCenter_.z;
    if (!sea_ || std::hypot(dx, dz) > kSeaRecenter) {
        seaCenter_ = {camera.x, 0.0f, camera.z};
        sea_ = std::make_unique<ColoredMesh>(seaQuad(seaCenter_));
    }
}

void CaveWorldView::draw(const Mat4& viewProjection, const ShaderProgram& colorShader, Vec3 eye,
                         Vec3 direction) const {
    terrainShader_.use();
    terrainShader_.setMat4("uViewProjection", viewProjection);
    terrainShader_.setVec2("uAltitudeRange", kStreamedLowAltitude, kStreamedHighAltitude);
    terrainShader_.setVec3("uEye", eye);
    terrainShader_.setVec3("uViewDirection", normalize(direction));
    // Triangles are counter-clockwise seen from the air: culling the back faces of the terrain
    // only, the near faces seen from inside the rock disappear.
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
    for (const auto& [key, view] : views_) {
        view->terrain.draw();
    }
    glDisable(GL_CULL_FACE);
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
}

std::string CaveWorldView::status() const {
    return tileStatus(views_, manager_);
}

} // namespace viewer
