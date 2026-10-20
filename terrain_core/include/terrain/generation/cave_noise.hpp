#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

// The 3D noises of the caves are evaluated at the nodes (i, j, k) * kCaveNodeMeters of a world
// lattice and interpolated trilinearly in between, as in the noise cells of Minecraft: the
// value at a point only depends on the world position, never on the area being generated, and
// a 4 m cell is bounded by its 8 nodes, so cells without caves are skipped exactly.
inline constexpr double kCaveNodeMeters = 4.0;

// The noises of the caves, one value each per node.
enum CaveNoise : std::size_t {
    kSpaghettiA,     // the tunnels follow the zero lines shared by A and B
    kSpaghettiB,
    kTunnelRadius,   // radius of the spaghetti and noodle tunnels
    kNoodleA,
    kNoodleB,
    kNoodlePresence, // noodles only where it is high enough
    kCheese,         // caverns where it is high
    kCaveNoiseCount,
};

using CaveNoiseValues = std::array<float, kCaveNoiseCount>;

// Bit of a noise in a mask of noises.
[[nodiscard]] constexpr unsigned caveNoiseBit(CaveNoise noise) {
    return 1U << static_cast<unsigned>(noise);
}

// One noise: fbm3D of (x, y * verticalScale, z) * frequency, meters.
struct CaveNoiseLayer {
    std::uint64_t seed = 0;
    double frequency = 0.01;
    double verticalScale = 1.0; // above 1, the shapes are flattened
    int octaves = 1;
};

// Pure node values of the cave noises.
class CaveNoiseLattice {
public:
    explicit CaveNoiseLattice(const std::array<CaveNoiseLayer, kCaveNoiseCount>& layers);

    // Value of the noise at the node (i, j, k), at world (i, j, k) * kCaveNodeMeters.
    [[nodiscard]] float nodeValue(CaveNoise noise, std::int64_t i, std::int64_t j,
                                  std::int64_t k) const;

    [[nodiscard]] const CaveNoiseLayer& layer(CaveNoise noise) const { return layers_[noise]; }

private:
    std::array<CaveNoiseLayer, kCaveNoiseCount> layers_;
};

// Box of world positions, meters: x in [minX, maxX] and so on.
struct CaveBox {
    double minX = 0.0;
    double minY = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    double maxZ = 0.0;
};

// Node values of a box, computed on first use and kept: every position of the box reads the
// same values as from the lattice itself, so a cache never changes a result.
class CaveNoiseCache {
public:
    CaveNoiseCache(const CaveNoiseLattice& lattice, const CaveBox& box);

    // Trilinear interpolation of the noises of the mask at (x, y, z), which must lie in the
    // box. Noises outside the mask are left unchanged.
    void sample(double x, double y, double z, unsigned noises, CaveNoiseValues& values);

    // Smallest and largest node value of the noises of the mask over the lattice cell holding
    // (x, y, z): every value interpolated in the cell lies between them.
    void cellBounds(double x, double y, double z, unsigned noises, CaveNoiseValues& low,
                    CaveNoiseValues& high);

    // Index of the lattice cell holding (x, y, z) among the cells of the box, in
    // [0, cellCount()).
    [[nodiscard]] std::size_t cellIndex(double x, double y, double z) const;
    [[nodiscard]] std::size_t cellCount() const { return known_.size(); }

private:
    struct Cell {
        std::int64_t i;
        std::int64_t j;
        std::int64_t k;
        double fx;
        double fy;
        double fz;
    };
    [[nodiscard]] Cell cellOf(double x, double y, double z) const;
    // Index of the node (i, j, k), with the noises of the mask computed.
    std::size_t node(std::int64_t i, std::int64_t j, std::int64_t k, unsigned noises);
    // Indices of the 8 nodes of the cell, x varying fastest, then y, then z.
    std::array<std::size_t, 8> corners(const Cell& cell, unsigned noises);

    const CaveNoiseLattice* lattice_;
    std::int64_t firstI_;
    std::int64_t firstJ_;
    std::int64_t firstK_;
    std::int64_t countI_;
    std::int64_t countJ_;
    std::int64_t countK_;
    std::vector<float> values_;        // node by node, kCaveNoiseCount values each
    std::vector<std::uint8_t> known_;  // bit n: value n of the node computed
};

} // namespace terrain
