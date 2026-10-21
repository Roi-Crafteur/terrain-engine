#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/io/relief_png.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain {

// Figures of a relief that do not depend on how it is generated, so that two reliefs can be
// compared: land masses, mountain chains, hypsometry, coasts, plains, mountains. They read a
// grid of heights of the whole world (sampleReliefWorld), and the Heightfield itself for the
// figures at the scale of a walk. Lengths and areas in meters and square meters.

struct ReliefStatsParams {
    // Mountain cells: land at least mountainHeight high whose heights within reliefRadius
    // span at least mountainRelief. Chains: the mountain cells closed over bridge (dilated
    // then eroded), so that the valleys inside a range do not cut it.
    float mountainHeight = 1000.0f;
    float mountainRelief = 800.0f;
    double reliefRadius = 20'000.0;
    double bridge = 32'000.0;
    // Plains: land at least plainDistance from every chain.
    double plainDistance = 500'000.0;
    // A stretch of coast is straight while every point of it lies within
    // max(straightTolerance, straightRelative * chord) of its chord.
    double straightTolerance = 10'000.0;
    double straightRelative = 0.03;
    // The straight stretches are also measured on the coast at the scale of a map of the
    // planet: the coast line of the share of land within coastSmoothing (a square of about
    // twice that side), without its islands and inlets.
    double coastSmoothing = 80'000.0;
    // Land masses of at least continentArea are continents (10 million km2).
    double continentArea = 1.0e13;
};

struct LandMass {
    double area = 0.0;
    double x = 0.0; // a cell of it
    double z = 0.0;
};

struct MountainChain {
    double length = 0.0; // longest shortest path inside the chain
    double width = 0.0;  // area / length
    double area = 0.0;
    float highest = 0.0f;
    double x = 0.0; // its highest cell
    double z = 0.0;
};

struct StraightCoast {
    double length = 0.0; // of its chord
    double x0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double z1 = 0.0;
};

// Upper bounds of the altitude bands of the land, meters; the last band has no bound.
inline constexpr std::array<float, 7> kAltitudeBands = {200.0f,  500.0f,  1000.0f, 2000.0f,
                                                        3000.0f, 4000.0f, 5000.0f};

// Bits of the class of a cell of the grid.
inline constexpr unsigned kReliefLand = 1;
inline constexpr unsigned kReliefMountain = 2; // a mountain cell
inline constexpr unsigned kReliefChain = 4;    // inside a chain (closed mountain cells)
inline constexpr unsigned kReliefPlain = 8;    // land far from every chain
inline constexpr unsigned kReliefCoast = 16;   // land next to the sea (4 neighbors)

// A place to look at closely: a cell of the grid and what it shows.
struct ReliefSite {
    bool found = false;
    double x = 0.0;
    double z = 0.0;
};

struct ReliefStats {
    double landShare = 0.0;
    std::vector<LandMass> landMasses; // largest first
    int continents = 0;
    std::vector<MountainChain> chains; // longest first
    std::array<double, kAltitudeBands.size() + 1> altitudeShares{}; // of the land area
    float highest = 0.0f;
    double highestX = 0.0;
    double highestZ = 0.0;
    double coastLength = 0.0; // of every coast line at the grid resolution
    StraightCoast longestStraightCoast;
    StraightCoast longestStraightMapCoast; // on the smoothed coast (coastSmoothing)
    double mountainShare = 0.0;  // of the land, in mountain cells
    double chainShare = 0.0;     // of the land, in chains
    double plainShare = 0.0;     // of the land, far from every chain
    double highPlainShare = 0.0; // of that land, above 1,500 m
    std::vector<std::uint8_t> cells; // kRelief* bits of each cell of the grid
    // The highest cell of the longest chain, the cell of a land mass of at least 1,000,000 km2
    // farthest from the chains (a plain when that distance reaches plainDistance), a coast
    // with as much land as sea around it near a chain (else anywhere).
    ReliefSite mountain;
    ReliefSite plain;
    double plainSiteDistance = 0.0; // from the plain site to the nearest chain
    ReliefSite coast;
};

// Throws std::invalid_argument unless the grid wraps (the whole world).
[[nodiscard]] ReliefStats measureRelief(const ReliefGrid& world,
                                        const ReliefStatsParams& params = {});

// Figures at the scale of a walk, from windows sampled in the Heightfield around up to
// windowCount cells of each kind (spread evenly over the cells of that kind).
struct LocalReliefStats {
    int plainWindows = 0;
    double plainSlope = 0.0;  // median of the mean slope, 1 km steps over 10 km windows
    double plainRelief = 0.0; // median of max - min over these windows
    int mountainWindows = 0;
    double mountainRelief = 0.0;   // median of max - min, 1 km steps over 10 km windows
    double mountainRelief90 = 0.0; // 90th percentile
    int coastWindows = 0;
    // Median of the length of the coast walked with 1 km dividers over its length with 10 km
    // dividers, on 40 km windows sampled every 200 m: 1 for a smooth coast.
    double coastSinuosity = 0.0;
};

[[nodiscard]] LocalReliefStats measureLocalRelief(const Heightfield& field,
                                                  const ReliefGrid& world,
                                                  const ReliefStats& stats, int windowCount,
                                                  int threads);

namespace relief_shapes {

// A polyline in meters.
struct Line {
    std::vector<double> x;
    std::vector<double> z;
    bool closed = false;
};

// The coast lines (height 0) of a grid by marching squares, linked into polylines; across the
// edges of a wrapping grid, each point is the image of its position nearest to the previous
// one, so that a line crossing the edge stays continuous.
[[nodiscard]] std::vector<Line> coastLines(const ReliefGrid& grid);

[[nodiscard]] double lineLength(const Line& line);

// The longest straight stretch of the lines (see ReliefStatsParams).
[[nodiscard]] StraightCoast longestStraight(const std::vector<Line>& lines, double tolerance,
                                            double relative);

// Length of the line walked with dividers of the given opening: whole steps, plus the
// remaining distance to its end.
[[nodiscard]] double dividerLength(const Line& line, double opening);

} // namespace relief_shapes

} // namespace terrain
