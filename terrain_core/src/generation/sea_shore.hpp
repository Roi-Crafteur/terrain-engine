#pragma once

// The distances to the shores at sea (agent 51), internal to terrain_core: exact Euclidean
// distance transforms on the torus of a grid of the world (kShoreCell meters), from the land of
// the coast signal of the geography at its nodes, read bilinearly: to any land, and to the large
// lands (land masses of at least bigArea: the continents and the large islands, whose margins have
// wide shelves; the small islands have narrow ones). Their gradient is about 1 everywhere: the
// margins keep their true slopes, where the coast signal, whose noises steepen it up to tenfold
// near the shores, would not. Deterministic, the same whatever the threads.

#include "geography_grid.hpp"
#include "terrain/world_size.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {
class Geography;
}

namespace terrain::sea_shore {

inline constexpr double kShoreCell = 16'384.0; // divides every side of a world
inline constexpr double kShoreUnit = 64.0;     // meters per unit of the stored distances

struct Shore {
    geography::Grid grid;
    WorldSize world;
    // The distance of each node to the nearest node of land (of large land) minus half a cell
    // (about the distance to the shore), in kShoreUnit meters, at most 65,535 (4,194 km); 0 on
    // land (on large land).
    std::vector<std::uint16_t> any;
    std::vector<std::uint16_t> big;

    [[nodiscard]] std::size_t memoryBytes() const {
        return (any.capacity() + big.capacity()) * 2 + sizeof(Shore);
    }
};

[[nodiscard]] Shore build(const Geography& geo, double bigArea, int threads);

// The distances to the shore of any land and of the large lands at the canonical point (x, z)
// of the world, meters.
struct Distances {
    double any = 0.0;
    double big = 0.0;
};
[[nodiscard]] Distances distancesAt(const Shore& s, double x, double z);

} // namespace terrain::sea_shore
