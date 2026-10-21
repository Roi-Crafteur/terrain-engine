#pragma once

#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

enum class SpeleothemKind : std::uint8_t { Stalactite, Stalagmite };

// A stalactite or a stalagmite: a cone of the given length and base radius, its base at
// (x, y, z) on the ceiling (a stalactite, tip at y - length) or on the floor (a stalagmite, tip
// at y + length). An object laid in the caves, like the plants on the surface: it never changes
// the density.
struct Speleothem {
    SpeleothemKind kind = SpeleothemKind::Stalactite;
    double x = 0.0;
    double z = 0.0;
    float y = 0.0f;
    float length = 0.0f;
    float radius = 0.0f;
    bool ice = false;

    friend bool operator==(const Speleothem&, const Speleothem&) = default;
};

// Deterministic speleothems (SpeleothemParams in caves.hpp): the same seed, parameters, caves and
// cell always give the same ones, wherever the area being placed is.
class SpeleothemField {
public:
    // Periodic on the world: the grid of the candidates has round(world / spacing) cells per
    // side (PeriodicCells), each drawn from its canonical cell. Throws std::invalid_argument for
    // an invalid world (validate).
    explicit SpeleothemField(std::uint64_t seed, const SpeleothemParams& params = {},
                             const WorldSize& world = {});

    // Speleothems of the cells whose candidate lies in [minX, maxX) x [minZ, maxZ], in cell
    // order, on the caves read from the sampler: adjacent rectangles give the speleothems of
    // their union. The sampler must carve every component (the detail of LOD 0) and its box must
    // hold the columns of the rectangle from maxDepth + 2 m below their ground to their ground.
    [[nodiscard]] std::vector<Speleothem> place(CaveSampler& sampler, double minX, double minZ,
                                                double maxX, double maxZ) const;

    [[nodiscard]] const SpeleothemParams& params() const { return params_; }
    [[nodiscard]] const PeriodicCells& cellsX() const { return cellsX_; }
    [[nodiscard]] const PeriodicCells& cellsZ() const { return cellsZ_; }

private:
    void placeCandidate(CaveSampler& sampler, int cellX, int cellZ,
                        std::vector<Speleothem>& result) const;

    SpeleothemParams params_;
    PeriodicCells cellsX_;
    PeriodicCells cellsZ_;
    std::uint64_t seed_;
};

} // namespace terrain
