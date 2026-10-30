#pragma once

// Internal to the settlement: the tectonic plates of the planetary geography, which it drops once
// built, made again from its own stages and seed, untouched (geography_boundaries.hpp): the warp
// of their boundaries at the nodes of its map, the kind of each boundary, the overriding plate of
// each collision, the share of land of each plate. Read only, for the geology in data: the relief
// never depends on it.

#include "../geography_boundaries.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/voronoi.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace terrain::settlement {

// The geography seed of a world: the layer kGeography (8) of the Heightfield.
[[nodiscard]] std::uint64_t geographySeed(std::uint64_t worldSeed);

// The crust of a geography as its map keeps it: land where the coast distance is above 0.
[[nodiscard]] geography::Crust crustOf(const Geography& geo);

class GeographyPlates {
public:
    // The plates of geo, built from worldSeed (the same seed as the Heightfield of geo).
    GeographyPlates(std::uint64_t worldSeed, const Geography& geo, int threads);
    // The fields of the boundaries keep a reference to the grid: never copied nor moved.
    GeographyPlates(const GeographyPlates&) = delete;
    GeographyPlates& operator=(const GeographyPlates&) = delete;

    // The two plates nearest to a point after the warp of the geography, a the nearest (the
    // lowest index on a tie), and the signed distance to their bisector toward b (meters, <= 0:
    // |towardB| is the distance to the boundary); boundary is the index of their boundary, or -1.
    struct Near {
        int a = 0;
        int b = 0;
        double towardB = 0.0;
        int boundary = -1;
    };
    // At (x, z) in the coordinates of the world (the map is read at x + anchorX), the warp
    // interpolated between the nodes of the map.
    [[nodiscard]] Near nearAt(double x, double z) const;
    // At node (i, j) of the map, its own warp.
    [[nodiscard]] Near nearAtNode(int i, int j) const;

    [[nodiscard]] const geography::Boundary& boundary(int index) const {
        return boundaries_[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] double landShare(int plate) const {
        return landShares_[static_cast<std::size_t>(plate)];
    }
    [[nodiscard]] const geography::Grid& grid() const { return grid_; }
    [[nodiscard]] const geography::BoundaryFields& fields() const { return *fields_; }
    [[nodiscard]] const TectonicPlates& plates() const { return *plates_; }
    [[nodiscard]] const Geography& geography() const { return *geo_; }
    [[nodiscard]] std::uint64_t seed() const { return seed_; }
    [[nodiscard]] const geography::Crust& crust() const { return crust_; }

private:
    [[nodiscard]] Near nearWarped(double wx, double wz) const;

    const Geography* geo_;
    std::uint64_t seed_;
    geography::Grid grid_;
    geography::Crust crust_;
    std::unique_ptr<TectonicPlates> plates_;
    std::unique_ptr<geography::BoundaryFields> fields_;
    std::vector<geography::Boundary> boundaries_;
    std::vector<double> landShares_;
    std::vector<double> offsetX_; // the warp at each node, meters
    std::vector<double> offsetZ_;
};

} // namespace terrain::settlement
