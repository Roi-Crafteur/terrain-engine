#pragma once

// The uploads of the updates of the streamer applied within a time per frame (agent 38). The
// uploads go one by one, nearest first as the update holds them; the real time is read after each
// and the frame stops once it reaches the budget, or once the next upload, estimated, would go
// beyond it; one upload a frame at least, so that loading never stops. An estimate too low thus
// makes the frame go beyond its budget by one upload at most. The moves and the removals of an
// update are applied once all its uploads are: a tile is never removed before the tiles replacing
// it are shown. An update not finished goes on at the next frame, before any newer one: the caller
// asks the streamer for a new update only once the pacer is free (busy() false).

#include "tile_streamer.hpp"
#include "unreal_frame.hpp"

#include <cstddef>
#include <functional>

namespace terrain::unreal {

// What applies an update to the meshes (in Unreal: the backend).
class UploadSink {
public:
    virtual ~UploadSink() = default;
    virtual void upload(const TileUpload& upload) = 0;
    virtual void move(const TileMove& move) = 0;
    virtual void remove(const TileKey& key) = 0;
};

// The budget of a frame: its time, the cost of an upload estimated before it (optional), and what
// learns from the time of each upload measured (optional).
struct UploadBudget {
    double milliseconds = 0.0; // 0 or less: the whole update within the frame
    std::function<double(const TileUpload&)> estimate;
    std::function<void(const TileUpload&, double)> measured;
};

// What a frame applied: its uploads, their vertices and the time they took (measured), and whether
// the update is applied whole (its moves and removals too).
struct PacedFrame {
    std::size_t uploads = 0;
    std::size_t vertices = 0;
    double milliseconds = 0.0;
    bool finished = false;
};

class UploadPacer {
public:
    using Clock = std::function<double()>; // milliseconds, never decreasing

    UploadPacer(); // the steady clock
    explicit UploadPacer(Clock clock);

    [[nodiscard]] bool busy() const { return busy_; }
    // The next update (busy() must be false); an empty one leaves the pacer free.
    void start(StreamerUpdate update);
    // The uploads of this frame within the budget; the moves and removals once all are applied.
    PacedFrame apply(UploadSink& sink, const UploadBudget& budget);
    // What is left of the update at once, whatever its time (before a warm-up, which applies
    // updates of its own: the order of the updates is kept).
    PacedFrame flush(UploadSink& sink);
    // An upload of the update not applied yet: its tile is not shown in Unreal, whatever the
    // streamer says (TileStreamer::shown).
    [[nodiscard]] bool holds(const TileKey& key) const;
    // The world origin moved by offset (ATerrainWorld::ApplyWorldOffset): the origins of the
    // uploads and moves left follow it.
    void shift(const UnrealPoint& offset);

private:
    PacedFrame run(UploadSink& sink, const UploadBudget& budget);

    Clock clock_;
    StreamerUpdate update_;
    std::size_t next_ = 0;
    bool busy_ = false;
};

} // namespace terrain::unreal
