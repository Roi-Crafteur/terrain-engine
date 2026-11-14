#pragma once

// Terrain.EditTiles <x> <z> (agent 41, tools of the developers): what the tiles over a point of
// the core hold, in the core and in Unreal, after the second test of #78 in Unreal (edits not seen
// after a Terrain.Goto, a hollow cut along the side of a tile) that the streaming of the bridge
// does not show (unreal_edit_streaming_test). A first line: the point the streaming follows, the
// camera and the pawn possessed, their chunks and the distances between them. Then one line per
// tile whose footprint holds the point (within a meter): its LOD and the LOD a player at the
// camera would ask for there, the ground of the column of the point in the data of the core and
// in the section the mesh of Unreal received (tile_column.hpp), their vertices, the bounds of the
// mesh, whether it is visible, and when it was last uploaded. Pure C++: the Unreal side only
// copies what its mesh holds (UTerrainMeshBackend::DescribeTile).

#include "tile_streamer.hpp"
#include "unreal_frame.hpp"

#include "terrain/streaming/lod.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace terrain::unreal {

// What a mesh of Unreal holds for a tile: its vertices as it received them, placed in the world
// (centimeters), its triangles, its bounds (world), whether it and its ground section are
// visible, and its uploads (the last one this long ago, and how many since it was created).
struct ShownSection {
    std::vector<UnrealPoint> positions;
    std::vector<std::uint32_t> triangles;
    UnrealPoint boundsMin;
    UnrealPoint boundsMax;
    bool visible = false;
    bool sectionVisible = false;
    double uploadedSecondsAgo = 0.0;
    int uploads = 0;
};

// The point the streaming follows (the focus of ATerrainWorld::Tick), the camera and the pawn
// possessed (its class), in Unreal; focusActor: the Terrain World streams around its FocusActor.
struct ViewPositions {
    UnrealPoint focus;
    UnrealPoint camera;
    std::optional<UnrealPoint> pawn;
    std::string pawnClass;
    bool focusActor = false;
};

// "EditTiles (x, z): streaming at (x, z) chunk (cx, cz), camera at (x, y, z) chunk (cx, cz) d m
// from it, pawn <class> at (x, y, z) d m from the camera".
[[nodiscard]] std::string positionsLine(const UnrealFrame& frame, double x, double z,
                                        const ViewPositions& view);

// True if the footprint of the tile (at this image) holds (x, z) within margin meters.
[[nodiscard]] bool tileHolds(const TileKey& image, double x, double z, double margin);

// The tiles the streamer shows over (x, z) (within margin): their canonical keys, nearest first.
[[nodiscard]] std::vector<TileKey> tilesOver(const TileStreamer& streamer, double x, double z,
                                             double margin = 1.0);

// The LOD of the tile over (x, z) in the selection of a player at the chunk camera (selectTiles,
// unwrapped coordinates), or -1 beyond the view distance.
[[nodiscard]] int expectedLodAt(const LodParams& params, ChunkCoord camera, double x, double z);

// One tile over the point: the streamer's view of it and the mesh of Unreal.
struct TileColumnReport {
    TileKey key;   // canonical
    TileKey image; // where it is shown
    int expectedLod = -1;
    bool inStreamer = false; // shown by the streamer, its data loaded
    std::optional<double> coreTop;
    std::size_t coreVertices = 0;
    std::size_t editedVertices = 0; // edit weight above 0
    bool inMesh = false; // a mesh of Unreal holds the tile
    std::optional<double> shownTop;
    std::size_t shownVertices = 0;
    CorePoint boundsLow; // the bounds of the mesh in the core
    CorePoint boundsHigh;
    bool pointInBounds = false;
    bool visible = false;
    bool sectionVisible = false;
    double uploadedSecondsAgo = 0.0;
    int uploads = 0;
};

// The report of a tile at (x, z) (unwrapped, near the camera): shown is what its mesh in Unreal
// holds, none without a mesh.
[[nodiscard]] TileColumnReport reportTile(const TileStreamer& streamer, const UnrealFrame& frame,
                                          const TileKey& key, double x, double z,
                                          ChunkCoord camera,
                                          const std::optional<ShownSection>& shown);

// "EditTiles (x, z): tile (cx, cz) LOD l (from the camera: LOD e) [image (ix, iz)]: core top 3.48
// m (812 vertices, 96 edited) | shown top 3.48 m (812 vertices, uploaded 2.3 s ago, upload 4),
// bounds x a..b y c..d z e..f (the point inside), visible".
[[nodiscard]] std::string editTilesLine(double x, double z, const TileColumnReport& report);

} // namespace terrain::unreal
