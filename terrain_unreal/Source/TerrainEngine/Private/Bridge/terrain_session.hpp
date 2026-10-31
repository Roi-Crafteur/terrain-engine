#pragma once

#include "edit_thread.hpp"
#include "load_timeline.hpp"
#include "planet_source.hpp"
#include "terrain_edits.hpp"
#include "tile_streamer.hpp"
#include "tile_timings.hpp"
#include "unreal_frame.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/storage/modifications.hpp"
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/horizon_edits.hpp"
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"
#include "terrain/world_size.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace terrain::unreal {

struct SessionParams {
    std::uint64_t seed = kDefaultWorldSeed;
    bool caves = false;       // the caves of CaveParams{} under the heightfield
    bool caveRegions = false; // with caves only: their regions (CaveDistrictParams{})
    WorldSize world;          // the wrapped world, of the tiles and of the streaming
    StreamParams stream;      // its world is replaced by world
    // The water of the world (phase 11B): the planet level of the seed in this file (written by
    // terrain_make_planet, storage/planet_file.hpp), built and written there when it does not
    // hold it, the reason told (openPlanetSource); its hydrology carves the rivers and fills the
    // lakes of the tiles and the horizon. Empty: the relief alone, without rivers or lakes.
    // UTF-8, as resolvePlanetPath gives it.
    std::string planetFile;
};

// The streamed world of an Unreal level: its tiles, the timings of their generations and
// conversions, their streamer, and the thread of its edits. Members are destroyed in reverse
// order, so the edit thread ends its edit and the workers of the streamer stop (once their
// current tile is generated) before the tiles they read go away.
class TerrainSession {
public:
    // Throws std::invalid_argument for an invalid world, like TileStreamer for invalid stream
    // parameters.
    explicit TerrainSession(const SessionParams& params);

    TerrainSession(const TerrainSession&) = delete;
    TerrainSession& operator=(const TerrainSession&) = delete;

    [[nodiscard]] const TerrainTiles& tiles() const { return tiles_; }
    // How the planet level was read or built (empty without a planet file).
    [[nodiscard]] const PlanetSource& planet() const { return planet_; }
    [[nodiscard]] TileTimings& timings() { return *timings_; }
    // The timeline of the loads (load_timeline.hpp): every generation of a tile is recorded in it.
    [[nodiscard]] LoadTimeline& timeline() { return *timeline_; }
    [[nodiscard]] TileStreamer& streamer() { return streamer_; }

    // Edits of the world (TerrainTiles): the tiles over the chunks they change are generated
    // again, and their ground is staged for the far horizon, which applies it between two of
    // its epochs. Each returns the chunks it changed; the edits throw as TerrainTiles, before
    // any change. They first wait for the queued edits and stage them (below).
    std::vector<ChunkCoord> applyEdit(const VolumeEdit& edit);
    std::vector<ChunkCoord> applyInSequence(std::span<const SequencedEdit> edits);
    std::vector<ChunkCoord> setBlock(BlockCoord block, BlockEdit edit);
    std::vector<ChunkCoord> clearBlock(BlockCoord block);
    std::vector<ChunkCoord> replaceModifications(ModificationStore store);
    // The edits as the far horizon sees them, for its streamer (HorizonMeshStreamer,
    // HorizonGpuStreamer): it outlives every horizon of the session.
    [[nodiscard]] HorizonEditMap& horizonEdits() { return horizonEdits_; }

    // The edits of the game, on the edit thread of the session (EditThread): they return at
    // once. The thread applies them to the tiles in their order, then reads the ground of their
    // columns for the far horizon. takeEdits, on the thread owning the session (once per frame),
    // generates again the tiles of the edits done and stages their ground, in their order, and
    // returns their outcomes: the bits of the edits above.
    void queueEdit(const VolumeEdit& edit, std::string label = {});
    // A ground edit at (x, z) (terrain_edits.hpp), its volume computed on the edit thread from
    // the heights there (groundEditAt).
    void queueGroundEdit(const GroundEditCommand& command, double x, double z,
                         std::string label = {});
    [[nodiscard]] std::vector<EditOutcome> takeEdits();
    // Until the queued edits are done; takeEdits still stages them.
    void waitEdits() { editThread_.wait(); }
    // Edits queued and not done.
    [[nodiscard]] std::size_t editsPending() const { return editThread_.pending(); }

    // The frame that places the ground of the core point (x, z) at the Unreal origin.
    [[nodiscard]] UnrealFrame frameAt(double x, double z) const;

    // Unreal Z of the ground (Heightfield::heightAt) under an Unreal position, centimeters.
    [[nodiscard]] double groundZ(const UnrealFrame& frame, const UnrealPoint& point) const;

private:
    std::vector<ChunkCoord> edited(std::vector<ChunkCoord> chunks);
    [[nodiscard]] EditOutcome runEdit(const std::function<VolumeEdit()>& volume, std::string label);
    void stageDone();
    void finishQueued();

    PlanetSource planet_; // first: the tiles read its hydrology
    TerrainTiles tiles_;
    std::shared_ptr<TileTimings> timings_;
    std::shared_ptr<LoadTimeline> timeline_;
    HorizonEditMap horizonEdits_;   // reads the relief of the tiles
    std::vector<EditOutcome> done_; // staged, not yet taken
    TileStreamer streamer_;
    EditThread editThread_; // last: destroyed first
};

} // namespace terrain::unreal
