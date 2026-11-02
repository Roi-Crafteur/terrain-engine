#pragma once

// A lake at the fine scale (phase 11B), internal to terrain_core. The planet level knows its lakes
// cell by cell (8 km): the relief within the cells may hold the water lower than their level, or
// let it out. A fine lake floods the relief from its seeds on a raster of nodes, 8-connected,
// within the region its caller allows: the nodes its seeds reach without rising to its level. Its
// level is lowered to the lowest relief where the water would leave the region, then to the
// lowest relief along the border of the squares of its nodes (sampled 4 times finer): its shore
// lies wherever the relief meets its level, never on the edge of a cell or of a square.

#include "terrain/generation/heightfield.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace terrain::water {

// The window of a lake, in the images of its seeds.
struct LakeWindow {
    double x0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double z1 = 0.0;
};

class FineLake {
public:
    struct Seed {
        double x = 0.0;
        double z = 0.0;
    };

    FineLake() = default;
    // The lake of level over relief from seeds (in the images of window), within the nodes of
    // window where inside holds (canonical coordinates). periodX and periodZ: the world.
    FineLake(const Heightfield& relief, const std::vector<Seed>& seeds, float level,
             const LakeWindow& window, const std::function<bool(double, double)>& inside,
             double periodX, double periodZ);

    // Its water, -infinity when it holds none.
    [[nodiscard]] float level() const { return level_; }
    // Whether (x, z), at any image, lies in a square of the raster with a node under the lake.
    [[nodiscard]] bool covers(double x, double z) const;
    [[nodiscard]] std::size_t bytes() const { return wet_.size() * sizeof(std::uint64_t); }
    // The box of the squares it may cover (covers is false out of it), at the images nearest to
    // its first node; empty (x1 < x0) when it holds no water.
    [[nodiscard]] LakeWindow window() const {
        if (wet_.empty()) {
            return {0.0, 0.0, -1.0, -1.0};
        }
        return {x0_ - step_, z0_ - step_, x0_ + nx_ * step_, z0_ + nz_ * step_};
    }
    [[nodiscard]] double periodX() const { return periodX_; }
    [[nodiscard]] double periodZ() const { return periodZ_; }

private:
    [[nodiscard]] bool wet(int i, int j) const;

    float level_ = -std::numeric_limits<float>::infinity();
    double x0_ = 0.0;
    double z0_ = 0.0;
    double step_ = 1.0;
    int nx_ = 0;
    int nz_ = 0;
    double periodX_ = 0.0;
    double periodZ_ = 0.0;
    std::vector<std::uint64_t> wet_;
};

} // namespace terrain::water
