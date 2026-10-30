#pragma once

// Internal to the settlement: what the points and the lines of the geology and of the magic give
// to the cells around them, intensity x bump(distance / reach), the largest of them where they
// overlap (an order that never matters).

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/settlement/telluric.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace terrain::settlement {

// (1 - t^2)^2 inside [-1, 1], 0 outside: 1 at the center, smooth to 0 at the reach.
[[nodiscard]] double bump(double t);

// Points of a wrapped world, each with its reach (meters) and intensity, in buckets of the largest
// reach: lookups only.
class PointField {
public:
    PointField(double periodX, double periodZ, double maxReach);
    void add(double x, double z, double reach, double intensity);
    // The largest intensity x bump(d / reach) of the points at (x, z), 0 if none reaches it.
    [[nodiscard]] double at(double x, double z) const;
    [[nodiscard]] bool empty() const { return count_ == 0; }

private:
    struct Point {
        double x;
        double z;
        double reach;
        double intensity;
    };
    [[nodiscard]] int bucketX(double x) const;
    [[nodiscard]] int bucketZ(double z) const;
    [[nodiscard]] std::uint64_t key(int bx, int bz) const;

    double periodX_;
    double periodZ_;
    int countX_ = 1;
    int countZ_ = 1;
    std::size_t count_ = 0;
    std::unordered_map<std::uint64_t, std::vector<Point>> buckets_;
};

// The telluric lines on the land cells: the largest intensity x bump(d / width) over the lines,
// d the distance from the center of the cell to the polyline.
[[nodiscard]] planet::LandValues<std::uint8_t>
stampLines(const PlanetGrid& grid, const std::shared_ptr<const planet::LandIndex>& land,
           const TelluricNetwork& network);

} // namespace terrain::settlement
