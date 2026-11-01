#pragma once

// Internal to the settlement: the points of the geology in data (volcanoes, arcane craters) and
// the spacing between points of a wrapped world.

#include "geology_plates.hpp"
#include "terrain/generation/settlement/geology.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terrain::settlement {

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

// The volcanoes of a world: in the arcs behind the subductions of oceanic plates, along the
// continental rifts and where the ridges reach the land, at hotspots, kept volcanoSpacingKm
// apart; the active and the large among them drawn from the seed.
[[nodiscard]] std::vector<Volcano> placeVolcanoes(const GeographyPlates& plates,
                                                  std::uint64_t worldSeed,
                                                  const GeologyRule& rule, double scale,
                                                  int threads);

// The arcane craters of a world, anywhere, the major ones first.
[[nodiscard]] std::vector<ArcaneCrater> placeCraters(const WorldSize& world,
                                                     std::uint64_t worldSeed,
                                                     const GeologyRule& rule, double scale);

} // namespace terrain::settlement
