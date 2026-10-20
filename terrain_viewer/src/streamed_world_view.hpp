#pragma once

#include "chunk_mesh.hpp"
#include "colored_mesh.hpp"
#include "math.hpp"
#include "shader.hpp"
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

struct GLFWwindow;

namespace viewer {

// Modification file of mode 9, in the working directory.
inline constexpr const char* kModificationFile = "terrain_modifications.bin";

// GPU copy of a loaded tile: its terrain in world meters, and a marker per plant.
struct TileView {
    explicit TileView(const terrain::TileData& data);

    ChunkMesh terrain;
    std::optional<ColoredMesh> plants;
    int lod = 0;
};

// The streamed world of modes 8 and 9: terrain::TerrainTiles of the default world, streamed
// around the camera by a terrain::StreamManager with worker threads, and the GPU copies of the
// loaded tiles, kept in step with the changes of each update. Mode 9 edits it (blocks and
// plants aimed at from the camera) and saves and reloads its modifications.
class StreamedWorldView {
public:
    StreamedWorldView(); // needs a current OpenGL context
    ~StreamedWorldView();

    StreamedWorldView(const StreamedWorldView&) = delete;
    StreamedWorldView& operator=(const StreamedWorldView&) = delete;

    // Above a summit of the world, south of it, looking north.
    [[nodiscard]] Vec3 spawn() const { return spawn_; }

    // Streams around the camera and uploads the tiles loaded by this update.
    void update(Vec3 camera);

    // Terrain tinted by LOD (mode 8) or not, plants, the sea and, when editing, a crosshair.
    void draw(const Mat4& viewProjection, const ShaderProgram& colorShader, bool tintLods,
              bool crosshair) const;

    // Mode 9 input, on key and button presses: left button digs the aimed block, right button
    // builds one against it, P plants a tree, O uproots the aimed plant, F5 saves and F9
    // reloads the modification file.
    void handleEditInput(GLFWwindow* window, Vec3 eye, Vec3 direction);

    // Reloads the modification file if it exists (entering mode 9).
    void reloadIfSaved();

    // Tiles per LOD, queue and, for mode 9, the edits and the last message.
    [[nodiscard]] std::string status(bool editing) const;

private:
    struct Aim;
    [[nodiscard]] std::optional<Aim> aim(Vec3 eye, Vec3 direction) const;
    void digBlock(const Aim& aim);
    void buildBlock(const Aim& aim);
    void plantTree(const Aim& aim);
    void uprootPlant(const Aim& aim);
    void save();
    void reload();
    void invalidate(const std::vector<terrain::ChunkCoord>& chunks);

    terrain::TerrainTiles tiles_;
    terrain::StreamManager manager_;
    ShaderProgram terrainShader_;
    std::unordered_map<terrain::TileKey, std::unique_ptr<TileView>, terrain::TileKeyHash> views_;
    std::unique_ptr<ColoredMesh> sea_;
    Vec3 seaCenter_;
    std::unique_ptr<ColoredMesh> crosshair_;
    Vec3 spawn_;
    std::array<bool, 6> held_{}; // edit keys and buttons down at the previous frame
    std::string message_;
};

} // namespace viewer
