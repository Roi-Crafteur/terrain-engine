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
// selected tile shown. Horizon, HorizonDetail: marked by the caller (first display of the far
// horizon, then its detail normals and crests all in).
enum class LoadMilestone : std::uint8_t {
    Ground,
    GroundCollision,
    Ring64,
    Ring256,
    AllTiles,
    Horizon,
    HorizonDetail
};
inline constexpr std::size_t kLoadMilestoneCount = 7;
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

// Whether the far horizon is placed around the core point (x, z), any image: its finest level
// covers it. False before the first commit, and after a jump beyond that level until the epoch
// planned around the new place is committed.
[[nodiscard]] bool horizonAround(const HorizonClipmap& clipmap, double x, double z);

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
    double worstFrameMilliseconds = 0.0;
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
    void recordFrame(double gameThreadMilliseconds, std::size_t uploads);
    // The milestones of the tiles, from the selection now. Returns those reached by this call.
    std::vector<LoadMilestone> observe(std::span<const SelectedTile> selection,
                                       LoadClock::time_point now);
    // Marks a milestone given by the caller; true if it was not reached before.
    bool mark(LoadMilestone milestone, LoadClock::time_point now);
    // Seconds since the start of the load (0 outside one).
    [[nodiscard]] double secondsSince(LoadClock::time_point now) const;

    [[nodiscard]] LoadSummary summary() const;
    [[nodiscard]] std::vector<TileTrace> traces() const; // nearest first
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

} // namespace terrain::unreal
