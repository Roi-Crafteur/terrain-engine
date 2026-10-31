#pragma once

// The phase of a load of the world around the player (agent 38): what the game does from the
// moment the player appears (the start, a teleport, a prepared point) until everything around is
// shown. Pure C++, the rhythm and the look only: no tile and no sample changes.
//
// - The boost: more tile workers, more horizon pieces at once and more uploads per frame while
//   loading, together within a cap, then back to the normal regime.
// - The fade: the screen held black until the ground under the feet, the 64 m ring, every tile
//   and the near levels of the horizon are shown, fadeMaxSeconds at most; what was still missing
//   then is told once.
// - The hold: the pawn kept still until the collision under it exists, however long it takes.
// - The prepared point: the focus of the streaming and of the horizon until released, and ready
//   once the tiles around it and the near levels of the horizon are shown.

#include "unreal_frame.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace terrain::unreal {

// The threads and the uploads of a frame.
struct LoadBudget {
    int workers = 2;              // tile workers taking tiles (StreamManager::setWorkerLimit)
    int pieces = 2;               // horizon pieces at once (HorizonEpochRunner)
    std::size_t uploadsPerFrame = 8; // tiles converted and uploaded by an update

    friend bool operator==(const LoadBudget&, const LoadBudget&) = default;
};

// The cap of the tile workers and horizon pieces together while loading, by default:
// min(hardwareThreads - 4, 12), at least the normal workers and pieces together.
[[nodiscard]] int defaultLoadingCap(unsigned hardwareThreads, const LoadBudget& normal);

struct LoadPhaseParams {
    LoadBudget normal;
    int cap = 12;                             // workers + pieces while loading
    std::size_t maxUploadsPerFrame = 32;      // while loading, at most
    double uploadMillisecondsPerFrame = 6.0;  // while loading: the uploads of a frame take this
    double fadeMaxSeconds = 2.0;
    double timeoutSeconds = 30.0; // the boost ends then, whatever is still loading
    int nearHorizonLevels = 4;    // the levels 0 to 3 of the horizon: the fade waits for them
    bool fade = true;
};

// What a frame sees around the player (or the prepared point).
struct LoadStatus {
    bool groundTiles = false;     // every tile within 1 chunk shown
    bool groundCollision = false; // a trace down hits a tile (or no pawn)
    bool ring64 = false;
    bool allTiles = false;        // every selected tile shown, none pending
    int horizonLevels = 0;        // levels of the horizon shown around the player
    int horizonLevelCount = 0;    // levels of the horizon, 0 without one
};

class LoadPhase {
public:
    // Throws std::invalid_argument for a cap below 1, uploads below the normal ones, a negative
    // time or near levels below 0.
    explicit LoadPhase(const LoadPhaseParams& params);

    // A load starts now (seconds on any steady clock): the boost, the fade and the hold.
    void begin(double now);
    // A load around a prepared point (x, z, core meters): the focus until release, no fade nor
    // hold (the pawn is elsewhere).
    void prepare(double x, double z, double now);
    void release();

    // Once per frame, after the streaming: the state of the load.
    void update(const LoadStatus& status, double now);

    [[nodiscard]] bool loading() const { return loading_; }   // the boost on
    [[nodiscard]] bool fading() const { return fading_; }     // the screen held black
    [[nodiscard]] bool holding() const { return holding_; }   // the pawn held still
    // The prepared point, while set: the focus of the streaming and of the horizon.
    [[nodiscard]] const std::optional<std::pair<double, double>>& prepared() const {
        return prepared_;
    }
    // A prepared point is ready: its tiles and the near levels of its horizon are shown. True once
    // (the frame it becomes ready), then false.
    [[nodiscard]] bool takeReady();
    // When the fade ended at its cap: what was missing (ground, 64 m, tiles, horizon 0-3), once.
    [[nodiscard]] std::vector<std::string> takeMissingAtCap();
    // Seconds since the start of the load (0 outside one).
    [[nodiscard]] double elapsed(double now) const { return started_ ? now - start_ : 0.0; }

    // The budget of this frame: normal outside a load; while loading, the tile workers take half
    // of the cap while tiles are pending (the horizon the rest), the horizon all but the normal
    // workers afterwards; the uploads of a frame as many as uploadMillisecondsPerFrame allows at
    // the mean cost of an upload, between the normal ones and maxUploadsPerFrame.
    [[nodiscard]] LoadBudget budget(bool tilesPending, double meanUploadMilliseconds) const;

    [[nodiscard]] const LoadPhaseParams& params() const { return params_; }

private:
    [[nodiscard]] bool nearShown(const LoadStatus& status) const;

    LoadPhaseParams params_;
    bool started_ = false;
    double start_ = 0.0;
    bool loading_ = false;
    bool fading_ = false;
    bool holding_ = false;
    bool ready_ = false;        // to tell (takeReady)
    bool readyTold_ = false;    // the prepared point was told ready
    std::optional<std::pair<double, double>> prepared_;
    std::vector<std::string> missing_; // to tell (takeMissingAtCap)
};

// The rotation of Unreal, degrees, that looks toward a compass heading (0 north, 90 east: the -z
// and the +x of the core) with a pitch (positive up, negative down, clamped to [-89.9, 89.9]),
// as Terrain.LoadTest sets the control rotation: yaw = heading - 90 in (-180, 180]. The same
// convention as the places of the tools of the developers (agent 36).
struct ViewAngles {
    double pitch = 0.0;
    double yaw = 0.0;
};
[[nodiscard]] ViewAngles viewAngles(double headingDegrees, double pitchDegrees);
// The unit vector of Unreal (X east, Y south, Z up) the view of these angles looks along.
[[nodiscard]] UnrealPoint lookDirection(const ViewAngles& angles);

} // namespace terrain::unreal
