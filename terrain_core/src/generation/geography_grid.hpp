#pragma once

// Periodic grid of the planetary geography and the operations its construction needs, internal
// to terrain_core: cubic B-spline sampling, exact Euclidean distance transform on the torus,
// connected components, blur, quantile. Everything is deterministic: plain loops in a fixed
// order, no library call whose result may differ between platforms.

#include "terrain/world_size.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace terrain::geography {

// Nodes over the whole world, a whole number along each side (PeriodicCells): node (i, j) at
// (-world.x / 2 + i sizeX, -world.z / 2 + j sizeZ), indices taken modulo the counts.
struct Grid {
    int nx = 1;
    int nz = 1;
    double sizeX = 1.0;
    double sizeZ = 1.0;

    [[nodiscard]] std::size_t nodes() const {
        return static_cast<std::size_t>(nx) * static_cast<std::size_t>(nz);
    }
    [[nodiscard]] int wrapI(int i) const { return (i % nx + nx) % nx; }
    [[nodiscard]] int wrapJ(int j) const { return (j % nz + nz) % nz; }
    [[nodiscard]] std::size_t index(int i, int j) const {
        return static_cast<std::size_t>(wrapJ(j)) * static_cast<std::size_t>(nx) +
               static_cast<std::size_t>(wrapI(i));
    }
    [[nodiscard]] int column(std::size_t node) const {
        return static_cast<int>(node % static_cast<std::size_t>(nx));
    }
    [[nodiscard]] int row(std::size_t node) const {
        return static_cast<int>(node / static_cast<std::size_t>(nx));
    }
    [[nodiscard]] double periodX() const { return nx * sizeX; }
    [[nodiscard]] double periodZ() const { return nz * sizeZ; }
    // Position of node (i, j) for an unwrapped i or j: continuous across the seams.
    [[nodiscard]] double x(int i) const { return -0.5 * periodX() + i * sizeX; }
    [[nodiscard]] double z(int j) const { return -0.5 * periodZ() + j * sizeZ; }
};

// The grid of nominal spacing on the world (validated), at least one node per side.
[[nodiscard]] Grid makeGrid(const WorldSize& world, double nominal);

// Uniform cubic B-spline: the weights of the nodes i - 1, i, i + 1, i + 2 at the fraction t of
// the span from node i to node i + 1 (positive, summing to 1, C2), and their derivatives
// along t.
struct Spline {
    std::array<double, 4> w;
    std::array<double, 4> dw;
};
[[nodiscard]] Spline bspline(double t);

// sum[f] += wx.w[a] wz.w[b] n[f] over the 4 x 4 nodes n = node(a, b) and their first `fields`
// fields f (rows b, then columns a, then fields f). Each field keeps its own sum, so vectorizing
// the loop over the fields changes no bit: Clang only vectorizes it over a constant count
// (Fields), MSVC only over a count it does not know (Fields = 0: `fields`).
template <std::size_t Fields, class Node, std::size_t Size>
void sumNodes(const Node& node, const Spline& wx, const Spline& wz, std::size_t fields,
              std::array<double, Size>& sum) {
    const std::size_t count = Fields > 0 ? Fields : fields;
    for (std::size_t b = 0; b < 4; ++b) {
        for (std::size_t a = 0; a < 4; ++a) {
            const float* n = node(a, b);
            const double w = wx.w[a] * wz.w[b];
            for (std::size_t f = 0; f < count; ++f) {
                sum[f] += w * n[f];
            }
        }
    }
}

// Where a coordinate falls in the grid along one axis: the node before it (unwrapped) and the
// fraction of the span.
struct Span {
    int node;
    double t;
};
// For a coordinate of the canonical range of a period divided into count spans of size.
[[nodiscard]] Span spanOf(double v, double period, double size);

// Seed of a node that is not a source of a distance transform.
inline constexpr double kNoSource = std::numeric_limits<double>::max();

// Squared Euclidean distance on the torus of the grid, in meters squared:
// min over the nodes q of |p - q|^2 + seed(q) for each node p, the distance between two nodes
// measured between their nearest images. seed holds initial squared distances (kNoSource where
// a node is not a source); kNoSource everywhere without any source. Felzenszwalb and
// Huttenlocher's lower envelope of parabolas, along the rows then the columns, each over three
// periods.
[[nodiscard]] std::vector<double> distanceTransform(const Grid& grid, std::vector<double> seed);

// 8-connected (or 4-connected) components of the member nodes on the torus: the component of
// each node (-1 outside), numbered in the order of their first node.
[[nodiscard]] std::vector<std::int32_t> components(const Grid& grid,
                                                   const std::vector<std::uint8_t>& member,
                                                   bool eightConnected, int& count);

// Mean over the square of 2 radius + 1 nodes around each node, on the torus (rows then columns).
void boxBlur(const Grid& grid, std::vector<float>& field, int radius);

// The value below which a fraction of the values lie: the element of rank
// floor(fraction * (n - 1)) in increasing order (unique, whatever the sort).
[[nodiscard]] float quantile(std::vector<float> values, double fraction);

} // namespace terrain::geography
