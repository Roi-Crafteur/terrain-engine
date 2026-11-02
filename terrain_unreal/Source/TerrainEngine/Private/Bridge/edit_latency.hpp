#pragma once
// Pure C++ layer of the Unreal plugin (no Unreal header): the time from the click of a player to
// the tile it changed shown again (agent 28). An edit is clicked, its outcome taken from the edit
// thread (done), then shown at the first upload of a tile of LOD 0 over one of its chunks; the
// times and the frames between them are kept for the last edits, summed up for the log and
// Terrain.EditStats.

#include "terrain/chunk_coord.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/world_size.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace terrain::unreal {

// One edit measured: seconds and frames from its click.
struct EditTiming {
    double doneSeconds = 0.0;  // its outcome taken on the game thread
    double shownSeconds = 0.0; // its tile uploaded
    std::int64_t frames = 0;   // frames from the click to the upload (0: the same frame)
};

// The last edits measured, milliseconds.
struct LatencySummary {
    std::size_t count = 0;
    double doneMedian = 0.0;
    double shownMedian = 0.0;
    double shownP95 = 0.0;
    double shownMax = 0.0;
    std::int64_t framesMedian = 0;
    std::int64_t framesMax = 0;
};

class EditLatency {
public:
    // Edits kept for the summary.
    explicit EditLatency(std::size_t kept = 256) : kept_(kept) {}

    // An edit of a player clicked at seconds (frame), queued on the edit thread; its id, for done.
    std::uint64_t clicked(double seconds, std::int64_t frame);
    // Its outcome taken, with the chunks it changed (canonical); an edit refused (no chunks) is
    // forgotten.
    void done(std::uint64_t id, std::span<const ChunkCoord> chunks, double seconds);
    // A tile uploaded to its mesh at seconds (frame): the edits done over one of its chunks are
    // shown when it is of LOD 0. Returns the edits it showed.
    std::size_t uploaded(const TileKey& tile, const WorldSize& world, double seconds,
                         std::int64_t frame);

    // Edits clicked and not yet shown.
    [[nodiscard]] std::size_t waiting() const { return waiting_.size(); }
    [[nodiscard]] const std::deque<EditTiming>& timings() const { return timings_; }
    [[nodiscard]] LatencySummary summary() const;

private:
    struct Waiting {
        std::uint64_t id = 0;
        double clickSeconds = 0.0;
        std::int64_t clickFrame = 0;
        bool done = false;
        double doneSeconds = 0.0;
        std::vector<ChunkCoord> chunks;
    };

    std::size_t kept_;
    std::uint64_t next_ = 1;
    std::vector<Waiting> waiting_;
    std::deque<EditTiming> timings_;
};

} // namespace terrain::unreal
