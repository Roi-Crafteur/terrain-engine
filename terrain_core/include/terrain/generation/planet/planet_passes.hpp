#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace terrain {

// The cols of the fine relief between neighboring cells of the planet level (phase 11B). Between
// the floors of two cells, 8 to 12 km apart, the relief can rise into a range the cells do not
// see: the water of one floor reaches the other only over the lowest col between them, and a
// river cuts its bed through it at most a cut deep. Where the col stands higher than that above
// both floors, the edge is held: the water passes it only once it stands as high as the col less
// the cut (a lake behind it), and otherwise goes its way around, through the valleys, by the
// lower cols. Heights in meters.
struct PlanetPassParams {
    bool enabled = false; // with the floors of the cells (PlanetParams::floorSamples above 1)
    double mountainCut = 300.0; // the deepest the rivers cut under the mountains
    double plainCut = 100.0;    // elsewhere
    // The ladder sees the relief every sectionStep meters: under the mountains a crest between two
    // sections stands up to this much higher than it sees (the courses see it every 128 m), and
    // the cut of an edge is kept this much below mountainCut.
    double margin = 60.0;
    // The col of an edge: the lowest of the highest ground over the ways of a ladder of sections
    // sectionStep apart, with halfWidth candidates lateralStep apart on each side of the straight
    // line (one aside per section at most): the water finds the lowest col within halfWidth *
    // lateralStep of it. The straight line first, at the sections: the ladder only where it
    // stands above the cut over the lower floor.
    double sectionStep = 500.0;
    double lateralStep = 500.0;
    int halfWidth = 3;
    // The cells whose edges are measured, every land cell when empty (a trial over a part of the
    // world: such a level is never written to a file).
    std::function<bool(std::size_t cell)> measured;
};

// What measureCols found.
struct PlanetPasses {
    std::size_t edges = 0;   // edges between land cells measured
    std::size_t ladders = 0; // of them, whose ladder was needed
    std::size_t held = 0;    // edges whose col holds the water above both floors
    float highest = 0.0f;    // meters: the most a col holds the water above the higher floor
};

namespace planet {

// The level the water must reach to pass each edge of the land: the col less its cut, in whole
// meters rounded up, for the measured cells (kNone elsewhere: the edge holds nothing).
class PlanetCols {
public:
    static constexpr std::int16_t kNone = std::numeric_limits<std::int16_t>::min();

    PlanetCols() = default;
    PlanetCols(const PlanetGrid& grid, const std::vector<std::uint8_t>& measured);

    // The level of the edge from cell to its neighbor k (kPlanetNeighbors), -infinity if none.
    [[nodiscard]] float level(std::size_t cell, int k) const;
    // Whether the edge holds the water above both floors (height), and at which level.
    [[nodiscard]] bool held(const std::vector<float>& height, std::size_t cell, int k,
                            float* at = nullptr) const;
    [[nodiscard]] bool empty() const { return levels_.empty(); }
    void set(std::size_t cell, int k, std::int16_t level);
    [[nodiscard]] std::size_t bytes() const;

private:
    [[nodiscard]] std::size_t slot(std::size_t cell) const; // kNoSlot when not measured

    PlanetGrid grid_;
    std::vector<std::uint64_t> words_;
    std::vector<std::uint32_t> rank_;
    std::vector<std::int16_t> levels_; // 8 per measured cell
};

// The crossing height from (ax, az) to (bx, bz) (the caller's images) over field.
[[nodiscard]] float crossingHeight(const Heightfield& field, double ax, double az, double bx,
                                   double bz, const PlanetPassParams& params);

// The cols of every edge between two land cells, one of them measured (params.measured), from
// floor to floor (floorOf): the cut of the mountains where one of the cells lies under them,
// the cut of the plains elsewhere. On up to threads threads, the same levels whatever their
// number.
[[nodiscard]] PlanetCols measureCols(const Heightfield& field, const PlanetGrid& grid,
                                     const std::vector<float>& height,
                                     const std::vector<std::uint8_t>& floor, int floorSamples,
                                     const std::vector<std::uint8_t>& ocean,
                                     const std::vector<std::uint8_t>& mountain,
                                     const PlanetPassParams& params, int threads,
                                     PlanetPasses& out);

} // namespace planet

} // namespace terrain
