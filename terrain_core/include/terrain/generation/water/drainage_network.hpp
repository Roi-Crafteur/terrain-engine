#pragma once

// The fine network of the streams (task 11C, step 1): where the water flows below the cells of
// 8 km of the planet level, from the sources and the ravines down to the trunks of the planet
// network (planet_network.hpp), on a lattice of 128 m over the relief.
//
// Coherent with the planet level: its trunks (the links of at least 4 cells or 10 m^3/s) are the
// outlets of the fine network, with the sea and the lakes; the water of a node is the yield of
// the planet level there; a point belongs to the basin of the trunk its water reaches, so that the
// divides follow the relief, not the grid of 8 km. The sources lie where the area drained passes
// the channel head of the climate, the ground and the slope (runoff_partition.hpp): dense on the
// wet impermeable slopes, rare on the dry permeable plains; a channel holds water all year where
// its base flow is enough (a brook), else it is a ravine or a wadi; the widths grow from almost
// nothing (7 Q^0.5, the law of the rivers of the planet level).
//
// Data only in step 1: the tiles do not carve these streams yet (step 2, with their valleys).
// Computed on demand cell by cell of the planet level and kept in a cache of bounded bytes, the
// least recently used dropped first: a block computed again gives the same bits. Deterministic,
// thread-safe.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_network.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain {

struct DrainageNetworkParams {
    std::size_t cacheBytes = 96u << 20; // the cache of the blocks and of the nodes of the cells
    int radius = 1;                      // the cells around a block it reads (window 3 x 3)
};

// A piece of a stream: a quadratic arc from (x0, z0) to (x1, z1) bent toward (cx, cz), meters, the
// images near the box asked for.
struct StreamPiece {
    double x0 = 0.0;
    double z0 = 0.0;
    double cx = 0.0;
    double cz = 0.0;
    double x1 = 0.0;
    double z1 = 0.0;
    float discharge = 0.0f; // m^3/s
    float width = 0.0f;     // meters
    std::uint8_t order = 0; // Strahler
    bool wet = false;
    bool trunk = false; // a trunk of the planet network
};

// A remarkable point of the network: a source (a channel head), a confluence, an end.
enum class StreamMarkKind : std::uint8_t { Source, Confluence, Mouth };
struct StreamMark {
    double x = 0.0;
    double z = 0.0;
    StreamMarkKind kind = StreamMarkKind::Source;
    StreamEnd end = StreamEnd::Confluence; // of a mouth
    bool wet = false;
};

// The network at a point.
struct NetworkPoint {
    bool stream = false;    // a stream within the limit
    double distance = 0.0;  // to its nearest node, meters
    float discharge = 0.0f;
    float width = 0.0f;
    float depth = 0.0f;
    std::uint8_t order = 0;
    bool wet = false;
    bool trunk = false;
    double heading = 0.0; // degrees clockwise from the north, toward which it flows
    StreamBank bank = StreamBank::None; // the bank of it the point lies on
    StreamKind kind = StreamKind::Brook;
    std::uint32_t basin = 0;     // the cell of the planet level where the water of the point ends
    float permeability = 0.0f;   // at the point
    float surface = 0.0f;        // the share of the yield that runs off there
    float sill = 0.0f;           // meters of a hollow its water fills to get out (0: none)
};

class DrainageNetwork {
public:
    // The network of planet over relief (the relief the planet level was built from), seed moving
    // its nodes.
    DrainageNetwork(std::uint64_t seed, const Heightfield& relief,
                    std::shared_ptr<const PlanetLevel> planet,
                    const DrainageNetworkParams& params = {});
    ~DrainageNetwork();
    DrainageNetwork(const DrainageNetwork&) = delete;
    DrainageNetwork& operator=(const DrainageNetwork&) = delete;

    [[nodiscard]] const PlanetNetwork& planetNetwork() const;
    // The pieces of the channels and trunks whose nodes lie in the box (canonical meters).
    [[nodiscard]] std::vector<StreamPiece> piecesIn(double minX, double minZ, double maxX,
                                                    double maxZ) const;
    [[nodiscard]] std::vector<StreamMark> marksIn(double minX, double minZ, double maxX,
                                                  double maxZ) const;
    [[nodiscard]] NetworkPoint at(double x, double z, double limit = 500.0) const;

    // The bytes of the cache now and at most since the start.
    [[nodiscard]] std::size_t cacheBytes() const;
    [[nodiscard]] std::size_t peakCacheBytes() const;
    // The blocks computed since the start (again after an eviction too).
    [[nodiscard]] std::size_t blocksComputed() const;

    struct Impl; // drainage_network.cpp

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace terrain
