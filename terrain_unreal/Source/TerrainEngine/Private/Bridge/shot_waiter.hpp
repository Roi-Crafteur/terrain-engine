#pragma once

#include "place_pose.hpp"
#include "tile_streamer.hpp"

#include "terrain/dev/places_file.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace terrain::unreal {

// The images of the tools of the developers (agent 36: Terrain.Shot, Terrain.Views): taken once
// what the camera sees is loaded, never half drawn, with a delay at most; and the names of their
// files and of their dated folders.

// What the streaming still has to do around the player.
struct StreamingState {
    std::size_t pendingTiles = 0;  // queued or generating (StreamManager::pendingCount)
    std::size_t selectedTiles = 0; // the tiles around the player (StreamManager::selection)
    std::size_t shownTiles = 0;    // of them, those shown
    bool horizonBusy = false;      // an epoch in flight or detail queued (GPU horizon)
    bool horizonShown = true;      // false while a horizon is not shown yet
    std::size_t editsPending = 0;

    friend bool operator==(const StreamingState&, const StreamingState&) = default;
};

// The tiles of a streamer: pending, selected and shown (the horizon and the edits left as they
// are).
[[nodiscard]] StreamingState tileState(const TileStreamer& streamer);

// Nothing left to load: every selected tile shown, none pending, the horizon shown and not
// busy, no edit pending.
[[nodiscard]] bool settled(const StreamingState& state);

struct ShotWaitParams {
    double leastSeconds = 0.5;   // after the pose, before anything is taken as settled
    double settleSeconds = 1.0;  // settled this long: the exposure and the lighting follow
    double timeoutSeconds = 60.0;
};

enum class ShotWait : std::uint8_t { Waiting, Ready, TimedOut };

// Waits for the streaming to settle after a pose: Ready once it has stayed settled for
// settleSeconds (after leastSeconds), TimedOut after timeoutSeconds otherwise. Called once per
// frame with the seconds since the pose.
class ShotWaiter {
public:
    // Throws std::invalid_argument for negative times or a timeout below leastSeconds.
    explicit ShotWaiter(const ShotWaitParams& params = {});

    ShotWait update(double elapsedSeconds, const StreamingState& state);
    [[nodiscard]] const ShotWaitParams& params() const { return params_; }

private:
    ShotWaitParams params_;
    double settledSince_ = -1.0; // -1: not settled
};

// The name of the dated folder of a run of Terrain.Views: "2026-10-31_14-05-09".
[[nodiscard]] std::string viewsFolderName(int year, int month, int day, int hour, int minute,
                                          int second);
// The file of the image of the index-th place of a run (from 1), in its order:
// "01_chute388_bassin.png".
[[nodiscard]] std::string viewFileName(std::size_t index, const std::string& place);

// What a run of Terrain.Views keeps of a view, in its views.txt: the place and its pose, how
// long it waited and whether it was ready, what the streaming still had to do then, the frames
// per second, and whether the planet file the place needs was missing.
struct ViewRecord {
    std::string file;
    dev::Place place;
    ViewPose pose;
    double waitedSeconds = 0.0;
    bool ready = false;
    StreamingState state;
    double framesPerSecond = 0.0;
    bool planetMissing = false;
};

// The first lines of views.txt: the world of the run (seed, preset, planet file, LOD rings).
[[nodiscard]] std::string viewsHeader(std::int64_t seed, const std::string& preset,
                                      const std::string& planetFile, const std::string& rings);
// One line of views.txt, for example
// "01_chute388_bassin.png chute388_bassin x -6185104 z 155924 eye 182.03 m yaw 137.00 pitch
// 30.00: ready after 7.4 s, 0 tiles pending, 214/214 shown, 58.1 fps".
[[nodiscard]] std::string describeView(const ViewRecord& record);

} // namespace terrain::unreal
