#pragma once

#include "place_pose.hpp"
#include "tile_streamer.hpp"

#include "terrain/dev/places_file.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
    // The load of a teleport still holds the view: the screen black or the pawn held (the
    // LoadPhase of agent 38: fading or holding).
    bool viewLoading = false;

    friend bool operator==(const StreamingState&, const StreamingState&) = default;
};

// The tiles of a streamer: pending, selected and shown (the horizon and the edits left as they
// are).
[[nodiscard]] StreamingState tileState(const TileStreamer& streamer);

// Nothing left to load: every selected tile shown, none pending, the horizon shown and not
// busy, no edit pending, the view no longer held black by a load.
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

// How an image was taken: the camera as Unreal really had it then (to set against the pose
// asked), the sizes of the viewport, of its render target and of the image. With a display
// scaled by Windows (200 %), the viewport may count its pixels at the scale of the desktop while
// its render target holds the physical ones: an image of the viewport's size then holds only the
// top left of the view (the horizon of a level view at its bottom edge).
struct CaptureCheck {
    bool measured = false;
    ViewPose camera;     // cameraPose of the PlayerCameraManager when the image was read
    double fov = 0.0;    // its horizontal field of view, degrees
    int viewportWidth = 0;
    int viewportHeight = 0;
    int textureWidth = 0; // the render target of the viewport, physical pixels
    int textureHeight = 0;
    int imageWidth = 0;   // the image written; 0: not known
    int imageHeight = 0;
    int cutTop = 0;       // rows cut off the top of the view (the border of the viewport widget)
    // "texture": its render target read; "viewport and its UI": its widget drawn by Slate;
    // "whole window": the window, by Unreal.
    std::string method;
};

// What a run of Terrain.Views keeps of a view, in its views.txt: the place and its pose, how
// long it waited and whether it was ready, what the streaming still had to do then, the frames
// per second, and whether the world had the planet file the place was measured with, or one it
// was measured without.
struct ViewRecord {
    std::string file;
    dev::Place place;
    ViewPose pose;
    double waitedSeconds = 0.0;
    bool ready = false;
    StreamingState state;
    double framesPerSecond = 0.0;
    PlanetMismatch planet = PlanetMismatch::None;
    CaptureCheck capture;
};

// What looks wrong in a capture: the camera turned more than 0.5 degree away from the pose, or
// an image whose size, with the rows cut off its top, is not the one of the render target of
// the view.
[[nodiscard]] std::vector<std::string> captureWarnings(const ViewRecord& record);

// The width and height of a PNG from its first 24 bytes (its signature and its IHDR chunk), or
// nullopt when they are not those of a PNG.
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
pngSize(std::span<const std::uint8_t> bytes);

// The first lines of views.txt: the world of the run (seed, preset, planet file, LOD rings).
[[nodiscard]] std::string viewsHeader(std::int64_t seed, const std::string& preset,
                                      const std::string& planetFile, const std::string& rings);
// One line of views.txt, for example
// "01_chute388_bassin.png chute388_bassin x -6185104 z 155924 eye 182.03 m yaw 137.00 pitch
// 30.00: ready after 7.4 s, 0 tiles pending, 214/214 shown, 58.1 fps", then, once measured,
// "; camera yaw 137.00 pitch 30.00 fov 90.0, image 3840x2160 (viewport 1920x1080, texture
// 3840x2160, read from the texture)" and its warnings.
[[nodiscard]] std::string describeView(const ViewRecord& record);

} // namespace terrain::unreal
