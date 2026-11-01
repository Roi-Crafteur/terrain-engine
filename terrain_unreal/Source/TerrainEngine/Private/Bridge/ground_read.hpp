#pragma once

// The ground of a teleport read on a thread (agent 38). The first read of a region builds its
// hydrology (202 ms measured in Unreal at the foot of a chain): it must not hold the game thread.
// The pawn is held meanwhile; it is placed once the level is read. The level to stand at is the
// ground, or the surface of the water of the core over it (a lake, a river, the sea) when it is
// higher: a pawn placed there is never under the water.

#include "terrain/generation/heightfield.hpp"

#include <cstddef>
#include <future>
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

// On the calling thread: Heightfield::sampleAt, its height and max(height, waterLevel).
[[nodiscard]] GroundLevel groundLevelAt(const Heightfield& field, double x, double z);

class GroundRead {
public:
    GroundRead() = default;
    GroundRead(const GroundRead&) = delete;
    GroundRead& operator=(const GroundRead&) = delete;
    ~GroundRead() = default; // waits for the reads still running (the field must outlive them)

    // Reads the level of (x, z) on a thread; a read still running is forgotten, never waited for.
    void ask(const Heightfield& field, double x, double z);
    // The read running is forgotten (a load that reads nothing begins).
    void forget();
    [[nodiscard]] bool pending() const { return current_.valid(); }
    // Once read, the level of the last point asked, given once; nothing before, nor after.
    [[nodiscard]] std::optional<GroundLevel> poll();
    // The reads still running, forgotten ones included.
    [[nodiscard]] std::size_t running() const;

private:
    void dropFinished();

    std::future<GroundLevel> current_;
    std::vector<std::future<GroundLevel>> forgotten_;
};

} // namespace terrain::unreal
