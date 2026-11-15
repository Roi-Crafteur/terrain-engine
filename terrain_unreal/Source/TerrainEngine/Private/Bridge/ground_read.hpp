#pragma once

// The ground of a teleport read on a thread (agent 38). The first read of a region builds its
// hydrology (202 ms measured in Unreal at the foot of a chain): it must not hold the game thread.
// The pawn is held meanwhile; it is placed once the level is read. The level to stand at is the
// ground, or the surface of the water of the core over it (a lake, a river, the sea) when it is
// higher: a pawn placed there is never under the water. The ground is the top of the rock the
// tiles draw (TerrainTiles::groundAt): behind a face in three dimensions (a wall of the mountains,
// a sea cliff) the relief of the heightmap lies up to half its height below it, in the rock.

#include "terrain/streaming/terrain_tiles.hpp"

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <vector>

namespace terrain::unreal {

// The ground of the core point (x, z) and the level to stand at there, meters; the time of the
// read, milliseconds.
struct GroundLevel {
    double x = 0.0;
    double z = 0.0;
    double ground = 0.0;
    double stand = 0.0;
    double milliseconds = 0.0;
};

// On the calling thread: TerrainTiles::groundAt, and max(it, the waterLevel of
// Heightfield::sampleAt).
[[nodiscard]] GroundLevel groundLevelAt(const TerrainTiles& tiles, double x, double z);

class GroundRead {
public:
    GroundRead() = default;
    GroundRead(const GroundRead&) = delete;
    GroundRead& operator=(const GroundRead&) = delete;
    // Waits for the reads still running (the tiles must outlive them), stopped first: a read
    // stops at its next stop point (work_stop.hpp: within milliseconds).
    ~GroundRead();

    // Reads the level of (x, z) on a thread; a read still running is forgotten, never waited for.
    void ask(const TerrainTiles& tiles, double x, double z);
    // The read running is forgotten (a load that reads nothing begins): stopped at once (its
    // flag raised: it leaves within milliseconds, its result never read), never waited for.
    void forget();
    [[nodiscard]] bool pending() const { return current_.result.valid(); }
    // Once read, the level of the last point asked, given once; nothing before, nor after.
    [[nodiscard]] std::optional<GroundLevel> poll();
    // The reads still running, forgotten ones included.
    [[nodiscard]] std::size_t running() const;

private:
    // A read on its thread and the flag that stops it (shared with the thread).
    struct Read {
        std::shared_ptr<std::atomic<bool>> stop;
        std::future<GroundLevel> result;
    };
    void dropFinished();

    Read current_;
    std::vector<Read> forgotten_;
};

} // namespace terrain::unreal
