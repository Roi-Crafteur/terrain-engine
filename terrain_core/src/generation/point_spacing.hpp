#pragma once

// Internal to terrain_core: points kept apart on the torus of a wrapped world (the volcanoes,
// volcano_placement.hpp; the arcane craters and the telluric nodes of the settlement).

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terrain {

// The shortest distance in meters between two points of a world wrapped with these periods.
[[nodiscard]] double wrappedDistance(double ax, double az, double bx, double bz, double periodX,
                                     double periodZ);

// Points kept apart by at least a distance, on the torus of a world: buckets of at least that
// size, the 3 x 3 around a point looked at. Lookups only: the result never depends on the order
// of the buckets.
class PointSpacing {
public:
    PointSpacing(double minDistance, double periodX, double periodZ);
    [[nodiscard]] bool farEnough(double x, double z) const;
    void add(double x, double z);

private:
    [[nodiscard]] std::uint64_t key(int bx, int bz) const;
    [[nodiscard]] int bucketX(double x) const;
    [[nodiscard]] int bucketZ(double z) const;

    double minDistance_;
    double periodX_;
    double periodZ_;
    int countX_ = 1;
    int countZ_ = 1;
    std::unordered_map<std::uint64_t, std::vector<std::pair<double, double>>> buckets_;
};

} // namespace terrain
