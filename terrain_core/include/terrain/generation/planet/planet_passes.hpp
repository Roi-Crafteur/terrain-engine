#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

// The sills of the fine relief across the links of the rivers of the planet level (phase 11B).
// Between the floors of two neighboring cells, 3 to 12 km apart, the relief can rise into a range
// the cells do not see: the valleys of the mountains are closed (their floors climb to passes and
// end in cirques), and the river would have to cut its valley through the crest. It may cut down
// to a limit; where the crest stands higher, the water is held behind it: the cell above becomes
// the sill of a lake, as high as the crest less the limit, and the water finds its way again from
// there (the lake spills over it, or elsewhere if lower). Heights in meters.
struct PlanetPassParams {
    bool enabled = false; // with the floors of the cells (PlanetParams::floorSamples above 1)
    double mountainCut = 300.0; // the deepest the rivers cut under the mountains
    double plainCut = 100.0;    // elsewhere
    // The ladder sees the relief every sectionStep meters: under the mountains a crest between two
    // sections stands up to this much higher than it sees (the courses see it every 128 m), and
    // the cut of a link is kept this much below mountainCut.
    double margin = 60.0;
    // The crossing of a link: the lowest of the highest ground over the ways of a ladder of
    // sections sectionStep apart, with halfWidth candidates lateralStep apart on each side of the
    // straight line (one aside per section at most): the river finds the lowest notch within
    // halfWidth * lateralStep of it.
    double sectionStep = 250.0;
    double lateralStep = 250.0;
    int halfWidth = 6;
    // The rivers are found again after the sills are raised, at most this many times.
    int iterations = 4;
};

// What raiseSills did.
struct PlanetPasses {
    std::size_t checked = 0;   // links whose crossing was computed
    std::size_t raised = 0;    // cells raised into the sills of lakes
    std::size_t rounds = 0;    // times the rivers were found again
    float highestRaise = 0.0f; // meters above the floor of the cell
};

namespace planet {

// The crossing height of the link from (ax, az) to (bx, bz) (the caller's images) over field.
[[nodiscard]] float crossingHeight(const Heightfield& field, double ax, double az, double bx,
                                   double bz, const PlanetPassParams& params);

// One link of a river to check: from cell to the cell next, from the point (ax, az) to (bx, bz).
struct PlanetLink {
    std::uint32_t cell = 0;
    std::uint32_t next = 0;
    double ax = 0.0;
    double az = 0.0;
    double bx = 0.0;
    double bz = 0.0;
};

// Raises the cells of the links whose crossing stands more than their cut above them (the
// mountain cut where mountain is 1) to that crossing less the cut. crossings keeps the crossing
// of every link computed (key cell * 9 + direction toward next), so that a link is computed once
// over the rounds. Links on up to threads threads: the same heights whatever their number.
// Returns the cells raised.
std::size_t raiseSills(const Heightfield& field, const PlanetGrid& grid,
                       const std::vector<PlanetLink>& links,
                       const std::vector<std::uint8_t>& mountain, const PlanetPassParams& params,
                       int threads, std::vector<float>& height,
                       std::vector<std::pair<std::uint64_t, float>>& crossings,
                       PlanetPasses& out);

} // namespace planet

} // namespace terrain
