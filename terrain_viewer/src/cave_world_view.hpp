#pragma once

#include "colored_mesh.hpp"
#include "material_mesh.hpp"
#include "math.hpp"
#include "shader.hpp"
#include "streamed_world_view.hpp"
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace viewer {

// Where the camera of the caves starts, and how far it looks down toward the north.
struct CaveSpawn {
    Vec3 position;
    float pitchDownDegrees = 0.0f;
};

// GPU copy of a loaded tile with caves, in world meters: the rock colored by material (the
// outer surface by altitude), the lakes of the caves, the speleothems and a marker per plant.
struct CaveTileView {
    explicit CaveTileView(const terrain::TileData& data);

    MaterialMesh rock;
    std::optional<MaterialMesh> lakes;       // translucent water or ice
    std::optional<MaterialMesh> speleothems; // stalactites and stalagmites
    std::optional<ColoredMesh> plants;
    int lod = 0;
};

using CaveTileViews =
    std::unordered_map<terrain::TileKey, std::unique_ptr<CaveTileView>, terrain::TileKeyHash>;

// The world of the caves mode, to look at: terrain::TerrainTiles of the default world with its
// caves, streamed around the camera by its own terrain::StreamManager (2 worker threads, default
// LOD rings; LOD 3 carves no cave), and the GPU copies of the loaded tiles, kept in step with
// the changes of each update. The rock is colored by the material of each vertex, the lakes are
// drawn translucent and speleothems hang from the ceilings and rise from the floors. No editing,
// no save.
class CaveWorldView {
public:
    CaveWorldView(); // needs a current OpenGL context
    ~CaveWorldView();

    CaveWorldView(const CaveWorldView&) = delete;
    CaveWorldView& operator=(const CaveWorldView&) = delete;

    // About 40 m south of the nearest cave entrance to the start of the streamed world and 25 m
    // above it, looking at it; without any entrance within a few km, the spawn of the streamed
    // world.
    [[nodiscard]] CaveSpawn spawn() const { return spawn_; }

    // Streams around the camera and uploads the tiles loaded by this update.
    void update(Vec3 camera);

    // The terrain with the sun and a headlight at eye along direction, the speleothems, the
    // plants, the sea and, last, the lakes. The rock is drawn with its back faces culled: from
    // inside the rock, the near faces are seen from behind, so the rock is seen through and the
    // caves are not. The speleothems and the lakes are drawn without culling; the lakes blend
    // over everything drawn before them, without writing the depth. The GL state is restored.
    void draw(const Mat4& viewProjection, const ShaderProgram& colorShader, Vec3 eye,
              Vec3 direction) const;

    // Tiles per LOD and queue.
    [[nodiscard]] std::string status() const;

private:
    terrain::TerrainTiles tiles_;
    terrain::StreamManager manager_;
    ShaderProgram terrainShader_;
    CaveTileViews views_;
    std::unique_ptr<ColoredMesh> sea_;
    Vec3 seaCenter_;
    CaveSpawn spawn_;
};

} // namespace viewer
