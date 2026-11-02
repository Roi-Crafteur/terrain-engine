#pragma once

// The timeline of a load of the world around the player (agent 38): from the moment the player
// appears (start of the game, teleport, prepared point) to the moment everything around it is
// shown. Pure C++, measures only: nothing here changes a tile.

#include "tile_streamer.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace terrain::unreal {

using LoadClock = std::chrono::steady_clock;

enum class LoadReason : std::uint8_t { Spawn, Teleport, Prepared };
[[nodiscard]] const char* loadReasonName(LoadReason reason);

// The milestones of a load. Ground: every tile within kGroundChunks of the chunk of the player
// shown (the LOD 0 tiles under its feet). GroundCollision: marked by the caller once a trace down
// from the player hits a tile. Ring64 and Ring256: every selected tile closer than 4 and 16
// chunks shown (the LOD 0 tiles, then up to LOD 2 with the default rings). AllTiles: every
// selected tile shown. HorizonNear, HorizonAll, HorizonDetail: marked by the caller (the levels 0
// to 3 of the far horizon shown around the player, then every level, then their detail normals
// and crests all in).
enum class LoadMilestone : std::uint8_t {
    Ground,
    GroundCollision,
    Ring64,
    Ring256,
    AllTiles,
    HorizonNear,
    HorizonAll,
    HorizonDetail
};
inline constexpr std::size_t kLoadMilestoneCount = 8;
// The levels of the horizon of HorizonNear: the levels 0 to 3, out to about 8 km.
inline constexpr int kNearHorizonLevels = 4;
[[nodiscard]] const char* milestoneName(LoadMilestone milestone);

inline constexpr int kGroundChunks = 1;
inline constexpr int kRing64Chunks = 4;
inline constexpr int kRing256Chunks = 16;

// The bands of the curve "distance of the tile -> instant it is shown": tile distances
// (tileDistance, whole chunks of 16 m) in [first, last] meters, the last band open.
inline constexpr std::array<int, 8> kLoadBandStartMeters = {0, 16, 32, 64, 128, 256, 384, 512};

// What the load waits for besides the tiles.
struct LoadExpectations {
    bool groundCollision = true;
    bool horizon = true;
    bool horizonDetail = true;
};

// A tile of the selection as the caller sees it: its distance to the chunk of the player (its
// nearest image, whole chunks) and whether it is shown.
struct SelectedTile {
    TileKey key;
    int distanceChunks = 0;
    bool shown = false;
};

// The selected tiles of the streamer around the player (an unwrapped chunk).
[[nodiscard]] std::vector<SelectedTile> selectedTiles(const TileStreamer& streamer,
                                                      ChunkCoord player);

// The rings complete in a selection: every tile within kGroundChunks shown (the ground), closer
// than kRing64Chunks and kRing256Chunks, every tile. Nothing complete in an empty selection.
struct TileRings {
    bool ground = false;
    bool ring64 = false;
    bool ring256 = false;
    bool all = false;
};
[[nodiscard]] TileRings tileRings(std::span<const SelectedTile> selection);

// Whether the far horizon is placed around the core point (x, z), any image: its finest level
// covers it. False before the first commit, and after a jump beyond that level until the epoch
// planned around the new place is committed.
[[nodiscard]] bool horizonAround(const HorizonClipmap& clipmap, double x, double z);

// Chebyshev distance between two chunks of the wrapped world, any images, in chunks: across the
// edges of the world as well. A jump of the player farther than its view in one frame is a
// teleport; a turn of the world (the same place) is a distance of 0.
[[nodiscard]] int wrappedChunkDistance(ChunkCoord a, ChunkCoord b, const WorldSize& world);

// What happened to one tile during the load, seconds since its start; negative: not yet. A
// generation started before the load has a negative start.
struct TileTrace {
    TileKey key;
    int distanceChunks = -1; // -1: never seen in the selection
    int thread = -1;         // index of the thread that generated it last
    int generations = 0;     // generations ended during the load (more than 1: generated again)
    double generationStart = -1.0;
    double generationEnd = -1.0;
    double converted = -1.0; // converted for Unreal by an update
    double shown = -1.0;     // its mesh set in Unreal
    double uploadMilliseconds = 0.0;
    bool collision = false;
};

// The parts of a frame of the game during a load, milliseconds: what the terrain does on the game
// thread, against the whole frame.
struct FrameTrace {
    double seconds = 0.0;                 // since the start of the load (set by recordFrame)
    double gameThreadMilliseconds = -1.0; // the whole frame (stat unit), -1: not known
    double terrainMilliseconds = 0.0;     // the tick of the Terrain World
    double tilesMilliseconds = 0.0;       // the update of the streamer: integrations, conversions
    std::size_t uploads = 0;
    double uploadsMilliseconds = 0.0;  // their meshes and collisions set in Unreal
    double horizonMilliseconds = 0.0;  // the update of the horizon: commits, texels prepared
    double texturesMilliseconds = 0.0; // the texels, water and normals sent, the levels placed
    int horizonLevels = 0;             // shown around the player
};

// The summary of a load, seconds since its start (-1: not reached).
struct LoadSummary {
    LoadReason reason = LoadReason::Spawn;
    double playerX = 0.0; // core meters
    double playerZ = 0.0;
    std::array<double, kLoadMilestoneCount> milestones{};
    // Per band of kLoadBandStartMeters: selected tiles, the instant the last one was shown (-1:
    // one at least is not), the mean generation of its tiles in milliseconds.
    std::array<std::size_t, kLoadBandStartMeters.size()> bandTiles{};
    std::array<double, kLoadBandStartMeters.size()> bandShown{};
    std::array<double, kLoadBandStartMeters.size()> bandGenerationMs{};
    std::size_t tiles = 0;          // traced
    std::size_t generatedAgain = 0; // tiles generated more than once
    std::vector<double> threadBusySeconds; // per generating thread
    std::vector<std::size_t> threadTiles;
    std::size_t frames = 0;
    FrameTrace worstFrame; // the longest frame on the game thread (or of the terrain, unknown)
    std::size_t mostUploadsPerFrame = 0;
    double uploadMilliseconds = 0.0; // total
    std::size_t uploads = 0;
};

// The timeline of the current load. Thread-safe: the generator wrapper (tracedGenerator) records
// from the workers, the rest from the thread owning the streamer. Outside a load (before begin,
// after the load completed) nothing is recorded but the generations, which are ignored.
class LoadTimeline {
public:
    void begin(LoadReason reason, double playerX, double playerZ, LoadClock::time_point now,
               const LoadExpectations& expectations = {});
    [[nodiscard]] bool active() const;
    // Every milestone expected is reached: the load is over (active() turns false).
    [[nodiscard]] bool complete() const;

    void recordGeneration(const TileKey& key, LoadClock::time_point start,
                          LoadClock::time_point end, std::thread::id thread);
    void recordConverted(const TileKey& key, LoadClock::time_point now);
    void recordShown(const TileKey& key, LoadClock::time_point now, double uploadMilliseconds,
                     bool collision);
    // A frame of the load; previousGameThreadMilliseconds, the whole previous frame (stat unit
    // reads it one frame late), goes to the previous frame (-1: unknown).
    void recordFrame(FrameTrace frame, LoadClock::time_point now,
                     double previousGameThreadMilliseconds = -1.0);
    // The milestones of the tiles, from the selection now. Returns those reached by this call.
    std::vector<LoadMilestone> observe(std::span<const SelectedTile> selection,
                                       LoadClock::time_point now);
    // Marks a milestone given by the caller; true if it was not reached before.
    bool mark(LoadMilestone milestone, LoadClock::time_point now);
    [[nodiscard]] bool reached(LoadMilestone milestone) const;
    // Seconds since the start of the load (0 outside one).
    [[nodiscard]] double secondsSince(LoadClock::time_point now) const;

    [[nodiscard]] LoadSummary summary() const;
    [[nodiscard]] std::vector<TileTrace> traces() const; // nearest first
    [[nodiscard]] std::vector<FrameTrace> frames() const; // in their order
    // The milestones expected and not reached, for the log.
    [[nodiscard]] std::vector<LoadMilestone> missing() const;

private:
    [[nodiscard]] double seconds(LoadClock::time_point time) const; // mutex_ held
    [[nodiscard]] bool expected(LoadMilestone milestone) const;     // mutex_ held
    [[nodiscard]] bool completeLocked() const;                      // mutex_ held
    bool reachMilestone(LoadMilestone milestone, double at);        // mutex_ held
    int threadIndex(std::thread::id thread);                        // mutex_ held

    mutable std::mutex mutex_;
    bool active_ = false;
    LoadSummary summary_;
    LoadExpectations expectations_;
    LoadClock::time_point start_{};
    std::unordered_map<TileKey, TileTrace, TileKeyHash> traces_;
    std::vector<FrameTrace> frames_;
    std::unordered_map<std::thread::id, int> threads_;
};

// The generator, each generation recorded in the timeline (start, end, thread); the tiles are
// unchanged. Throws std::invalid_argument for an empty generator or a null timeline.
[[nodiscard]] TileGenerator tracedGenerator(TileGenerator generator,
                                            std::shared_ptr<LoadTimeline> timeline);

// The summary for the log: the milestones, the curve per band, the threads and the frames.
[[nodiscard]] std::vector<std::string> describe(const LoadSummary& summary);
// One line per milestone reached, "ground 0.031 s", for a log line written when it is reached.
[[nodiscard]] std::string describeMilestone(LoadMilestone milestone, double seconds);
// The traces as CSV, header first.
[[nodiscard]] std::vector<std::string> csvRows(std::span<const TileTrace> traces);
// The frames as CSV, header first (load_frames.cpp).
[[nodiscard]] std::vector<std::string> frameRows(std::span<const FrameTrace> frames);
// "299.0 ms at 3.210 s (terrain 12.3 ms: tiles 1.2, 32 uploads 6.0, horizon 3.1, textures 2.0;
// 4 levels)": a frame for the log (load_frames.cpp).
[[nodiscard]] std::string describeFrame(const FrameTrace& frame);
// The frame of the summary: the longest on the game thread, or of the terrain where the frames
// are not known (load_frames.cpp).
[[nodiscard]] FrameTrace worstFrame(std::span<const FrameTrace> frames);

} // namespace terrain::unreal
