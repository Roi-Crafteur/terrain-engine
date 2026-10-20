#include "cave_world_view.hpp"

#include "cave_look.hpp"
#include "flora_look.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <optional>
#include <vector>

namespace viewer {

namespace {

// Meshes lit as the terrain of the streamed world (sun), plus a headlight at uEye along
// uViewDirection: a wide soft cone, fading with the distance, so that the ceilings and walls of
// the caves are lit. The albedo is the color of the vertex, its alpha the opacity of the mesh.
// The faces seen from behind (lakes and speleothems are drawn without culling) are lit by their
// other side. uEmissive (0 for everything but the lava, 1 for the lava) is the share of the
// albedo shown unlit: the lava glows, in the dark as under the headlight (glowColor, cave_look,
// mirrors this mix).
constexpr const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec3 vPosition;
out vec4 vColor;
void main() {
    vNormal = aNormal;
    vPosition = aPosition;
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 330 core
in vec3 vNormal;
in vec3 vPosition;
in vec4 vColor;
uniform vec3 uEye;
uniform vec3 uViewDirection;
uniform float uEmissive;
out vec4 fragColor;
const float kHeadlightRange = 40.0;
const float kHeadlightStrength = 0.9;
void main() {
    vec3 normal = normalize(vNormal);
    if (!gl_FrontFacing) {
        normal = -normal;
    }
    vec3 lightDirection = normalize(vec3(0.4, 1.0, 0.3));
    float diffuse = max(dot(normal, lightDirection), 0.0);

    vec3 toEye = uEye - vPosition;
    float distance = max(length(toEye), 0.001);
    float facing = max(dot(normal, toEye / distance), 0.0);
    float cone = smoothstep(0.2, 0.9, dot(-toEye / distance, normalize(uViewDirection)));
    float range = distance / kHeadlightRange;
    float headlight = kHeadlightStrength * facing * cone / (1.0 + range * range);
    vec3 lit = vColor.rgb * (0.25 + 0.75 * diffuse + headlight);
    fragColor = vec4(mix(lit, vColor.rgb, uEmissive), vColor.a);
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

// Tells whether the camera may start at an entrance; empty: any.
using EntranceFilter = std::function<bool(const terrain::CaveEntrance&)>;

// The entrance accepted by the filter whose top is nearest to (x, z), within kSearchMax meters.
std::optional<terrain::CaveEntrance> nearestEntrance(const terrain::TerrainTiles& tiles,
                                                     terrain::AquiferCache& aquifers, double x,
                                                     double z, const EntranceFilter& accept) {
    const terrain::CaveField* caves = tiles.caves();
    if (caves == nullptr) {
        return std::nullopt;
    }
    for (double radius = kSearchStart; radius <= kSearchMax; radius *= 2.0) {
        const std::vector<terrain::CaveEntrance> found = caves->entrancesNear(
            x - radius, z - radius, x + radius, z + radius, tiles.caveGround(), aquifers);
        std::optional<terrain::CaveEntrance> nearest;
        double best = radius; // a nearer entrance than the square's half-side is always found
        for (const terrain::CaveEntrance& entrance : found) {
            const double distance = std::hypot(entrance.topX - x, entrance.topZ - z);
            if (distance <= best && (!accept || accept(entrance))) {
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

// The type of the district (the nearest site) at (x, z); the cave regions must exist.
terrain::CaveDistrictType districtTypeAt(terrain::AquiferCache& aquifers, double x, double z) {
    return aquifers.districtWeights(x, z).districts[0].type;
}

// Entrances on land (the ground over the sea) whose top and bottom are not in a flooded district.
EntranceFilter dryLandFilter(const terrain::TerrainTiles& tiles, terrain::AquiferCache& aquifers) {
    return [&tiles, &aquifers](const terrain::CaveEntrance& entrance) {
        const auto flooded = [&aquifers](double x, double z) {
            return districtTypeAt(aquifers, x, z) == terrain::CaveDistrictType::Flooded;
        };
        return tiles.heightfield().heightAt(entrance.topX, entrance.topZ) > terrain::kSeaLevel &&
               !flooded(entrance.topX, entrance.topZ) &&
               !flooded(entrance.bottomX, entrance.bottomZ);
    };
}

// The camera kSpawnSetback meters south of the top of the entrance, looking at it.
CaveSpawn spawnAt(const terrain::TerrainTiles& tiles, const terrain::CaveEntrance& entrance) {
    const double x = entrance.topX;
    const double z = entrance.topZ + kSpawnSetback;
    const double ground = tiles.heightfield().heightAt(x, z);
    const double y = std::max(entrance.topY + kSpawnHeight, ground + kSpawnClearance);
    const double pitch = std::atan2(y - entrance.topY, kSpawnSetback);
    return {{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
            static_cast<float>(pitch * 180.0 / std::numbers::pi)};
}

CaveSpawn spawnPoint(const terrain::TerrainTiles& tiles, terrain::AquiferCache& aquifers) {
    const Vec3 start = streamedSpawn(tiles.heightfield());
    const bool regions = tiles.caves()->districts() != nullptr;
    std::optional<terrain::CaveEntrance> entrance;
    if (regions) {
        entrance = nearestEntrance(tiles, aquifers, start.x, start.z,
                                   dryLandFilter(tiles, aquifers));
    }
    if (!entrance) {
        if (regions) {
            std::fprintf(stderr, "No dry cave entrance within %.0f m: any entrance will do\n",
                         kSearchMax);
        }
        entrance = nearestEntrance(tiles, aquifers, start.x, start.z, {});
    }
    if (!entrance) {
        std::fprintf(stderr, "No cave entrance within %.0f m: starting as the streamed world\n",
                     kSearchMax);
        return {start, kStreamedPitchDegrees};
    }
    std::fprintf(stderr, "Cave entrance at (%.0f, %.0f, %.0f), chamber %.0f m deep",
                 entrance->topX, entrance->topY, entrance->topZ,
                 entrance->topY - entrance->bottomY);
    if (regions) {
        const terrain::CaveDistrictType type = districtTypeAt(aquifers, entrance->topX,
                                                              entrance->topZ);
        std::fprintf(stderr, ", in a %s district", terrain::caveDistrictName(type));
    }
    std::fprintf(stderr, "\n");
    return spawnAt(tiles, *entrance);
}

MeshOrigin originOf(const terrain::TileData& data) {
    return {data.originX, data.originY, data.originZ};
}

// Drops the views of the unloaded tiles, copies the loaded ones (or replaces them).
void applyCaveChanges(CaveTileViews& views, const terrain::StreamManager& manager,
                      const terrain::StreamChanges& changes, const DistrictColorAt& districtColor) {
    for (const terrain::TileKey& key : changes.unloaded) {
        views.erase(key);
    }
    for (const terrain::TileKey& key : changes.loaded) {
        views[key] = std::make_unique<CaveTileView>(*manager.find(key), districtColor);
    }
}

// Draws the lava, opaque and unlit (emissive), without culling, then restores the lit look for
// the meshes drawn after it: the terrain shader must be in use. The uniform keeps its value
// between draws, so it is set back to 0 here and set to 0 again at the start of each frame.
void drawLava(const CaveTileViews& views, const ShaderProgram& terrainShader) {
    terrainShader.setFloat("uEmissive", kLavaEmissive);
    for (const auto& [key, view] : views) {
        if (view->lava) {
            view->lava->draw();
        }
    }
    terrainShader.setFloat("uEmissive", 0.0f);
}

// Blends the lakes over what is drawn: no culling (seen from above and from below), no depth
// writes (the lakes do not hide each other nor what is behind them). The depth test stays.
void drawLakes(const CaveTileViews& views) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const auto& [key, view] : views) {
        if (view->lakes) {
            view->lakes->draw();
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace

CaveTileView::CaveTileView(const terrain::TileData& data, const DistrictColorAt& districtColor)
    : rock(data.mesh, districtRockColors(data, districtColor), originOf(data)),
      lod(data.request.key.lod) {
    if (!data.water.indices.empty()) {
        lakes.emplace(data.water, lakeColors(data), originOf(data), kLakeOpacity);
    }
    if (!data.lava.indices.empty()) {
        lava.emplace(data.lava, lavaColors(data), originOf(data));
    }
    if (!data.speleothems.empty()) {
        const SpeleothemMesh cones = speleothemMesh(data.speleothems);
        speleothems.emplace(cones.mesh, cones.colors);
    }
    if (!data.caveFlora.empty()) {
        const PaintedMesh leaves = caveFloraMesh(data.caveFlora);
        if (!leaves.mesh.indices.empty()) {
            flora.emplace(leaves.mesh, leaves.colors);
        }
    }
    if (!data.plants.empty()) {
        plants.emplace(plantMarkers(data.plants));
    }
}

CaveWorldView::CaveWorldView(const terrain::CaveParams& caves)
    : tiles_(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, terrain::EcologyParams{},
             caves),
      aquifers_(*tiles_.caves(), tiles_.caveGround()),
      manager_(terrain::tileGenerator(tiles_), streamParams()),
      terrainShader_(kVertexShader, kFragmentShader), spawn_(spawnPoint(tiles_, aquifers_)) {
    if (regions()) {
        districtColor_ = [this](double x, double z) {
            return districtMixColor(aquifers_.districtWeights(x, z));
        };
    }
    std::fprintf(stderr,
                 "Streaming the world with its caves%s around the camera (seed %llu, %d worker "
                 "threads)\n",
                 regions() ? " and cave regions" : "",
                 static_cast<unsigned long long>(tiles_.seed()), manager_.params().workerThreads);
}

CaveWorldView::~CaveWorldView() = default;

void CaveWorldView::update(Vec3 camera) {
    const terrain::StreamChanges changes = manager_.update(camera.x, camera.z);
    applyCaveChanges(views_, manager_, changes, districtColor_);
    const float dx = camera.x - seaCenter_.x;
    const float dz = camera.z - seaCenter_.z;
    if (!sea_ || std::hypot(dx, dz) > kSeaRecenter) {
        seaCenter_ = {camera.x, 0.0f, camera.z};
        sea_ = std::make_unique<ColoredMesh>(seaQuad(seaCenter_));
    }
    if (regions()) {
        districtLabel_ = districtMixLabel(aquifers_.districtWeights(camera.x, camera.z));
    }
}

void CaveWorldView::draw(const Mat4& viewProjection, const ShaderProgram& colorShader, Vec3 eye,
                         Vec3 direction) const {
    terrainShader_.use();
    terrainShader_.setMat4("uViewProjection", viewProjection);
    terrainShader_.setVec3("uEye", eye);
    terrainShader_.setVec3("uViewDirection", normalize(direction));
    terrainShader_.setFloat("uEmissive", 0.0f); // lit: only the lava glows
    // Triangles are counter-clockwise seen from the air: culling the back faces of the rock
    // only, the near faces seen from inside the rock disappear.
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
    for (const auto& [key, view] : views_) {
        view->rock.draw();
    }
    glDisable(GL_CULL_FACE);
    for (const auto& [key, view] : views_) {
        if (view->speleothems) {
            view->speleothems->draw();
        }
        if (view->flora) {
            view->flora->draw();
        }
    }
    drawLava(views_, terrainShader_);
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
    terrainShader_.use(); // the lakes come last: translucent, over everything opaque
    drawLakes(views_);
}

std::string CaveWorldView::status() const {
    std::array<int, terrain::kLodCount> counts{};
    int lavaTiles = 0;
    for (const auto& [key, view] : views_) {
        ++counts[static_cast<std::size_t>(view->lod)];
        lavaTiles += view->lava ? 1 : 0;
    }
    std::string text = tileCountStatus(counts, manager_.pendingCount());
    if (lavaTiles > 0) {
        text += ", lava in " + std::to_string(lavaTiles);
    }
    return districtLabel_.empty() ? text : districtLabel_ + " - " + text;
}

} // namespace viewer
