#pragma once

#include "terrain/generation/heightfield.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace terrain {

class Seas;

// Figures of the floors of the seas (agent 51), to compare them with the bathymetry of the Earth:
// the hypsometry of the ocean (the share of its area in bands of depth), its mean and median
// depths, the deepest point, the floor far from every land (the high floors of the ridges among
// them), profiles from the shore out to sea (shelf break, continental slope, rise, trench) and the
// roughness of the deep floor at the scale of a walk. They read a grid of the whole world
// (sampleReliefWorld: the water of the samples decides the sea), then the Heightfield along the
// profiles and in windows. Depths in meters below the sea level (positive), lengths in meters.

// Upper bounds of the bands of depth of the hypsometry, meters below the sea; the last band has
// no bound.
inline constexpr std::array<float, 8> kSeaDepthBands = {200.0f,  1000.0f, 2000.0f, 3000.0f,
                                                        4000.0f, 5000.0f, 6000.0f, 7000.0f};
inline constexpr std::size_t kSeaBandCount = kSeaDepthBands.size() + 1;

// The hypsometry of the oceans of the Earth in the same bands: shares of the area of all the
// oceans with their adjacent seas (Kossinna, 1921, as tabulated in Sverdrup, Johnson and Fleming,
// The Oceans, 1942, chapter 2), and their mean depth there (3,795 m).
inline constexpr std::array<double, kSeaBandCount> kEarthSeaBands = {
    0.076, 0.043, 0.042, 0.068, 0.196, 0.330, 0.233, 0.011, 0.001};
inline constexpr double kEarthMeanDepth = 3795.0;

struct SeaStatsParams {
    // The cells of the grid of the world (they must divide both its sides).
    double cellSize = 16'384.0;
    // At most this many profiles, spread evenly over the cells of the sea next to the land.
    int transects = 256;
    // Their step and length; a profile that meets land again before minLength is dropped.
    double transectStep = 1'000.0;
    double transectLength = 800'000.0;
    double minLength = 300'000.0;
    // The shelf ends where the floor first falls more steeply than breakGradient over
    // breakWindow (the shelf break); the continental slope is measured from it down slopeDrop
    // meters, the rise from riseTop to riseBottom meters, a trench within trenchReach.
    double breakGradient = 0.0175; // 1 degree
    double breakWindow = 5'000.0;
    double slopeDrop = 1'500.0;
    double riseTop = 3'000.0;
    double riseBottom = 4'000.0;
    double trenchReach = 400'000.0;
    // The open floor: the cells of the sea at least openDistance from every land.
    double openDistance = 600'000.0;
    // The roughness of the deep floor: windows of hillSamples x hillSamples samples hillSpacing
    // apart on open floor deeper than hillDepth, their rms about their best plane.
    int hillWindows = 32;
    int hillSamples = 64;
    double hillSpacing = 250.0;
    double hillDepth = 3'000.0;
    int threads = 0; // 0: all the cores
    // The seas laid over the sea of a field without hydrology (built without a planet level:
    // terrain_seas_stats); nullptr: those of the hydrology of the field, if any.
    const Seas* seas = nullptr;
};

// A profile from a cell of the sea next to the land, away from it (down the distance to the land).
struct SeaTransect {
    double x = 0.0;
    double z = 0.0;
    double heading = 0.0; // degrees from the north (-z), clockwise
    double length = 0.0;  // how far it went before meeting land again or its length
    bool shelfBreak = false;
    double breakDepth = 0.0;
    double breakDistance = 0.0;
    bool slope = false;
    double slopeDegrees = 0.0; // from the shelf break down slopeDrop meters
    bool rise = false;
    double riseDegrees = 0.0; // from riseTop to riseBottom meters
    double deepest = 0.0;     // within trenchReach
    double deepestDistance = 0.0;
};

// Percentiles of a figure over the profiles that have it.
struct SeaSpread {
    std::size_t count = 0;
    double p10 = 0.0;
    double median = 0.0;
    double p90 = 0.0;
};

struct SeaStats {
    std::size_t cells = 0;
    std::size_t seaCells = 0;
    std::array<double, kSeaBandCount> bands{}; // shares of the sea cells
    double meanDepth = 0.0;
    double medianDepth = 0.0;
    double deepest = 0.0;
    double deepestX = 0.0;
    double deepestZ = 0.0;
    // The open floor: its cells, median depth, and the depth above which lie its 5 % shallowest
    // cells (the crests of the ridges and the seamounts, without the islands).
    std::size_t openCells = 0;
    double openMedianDepth = 0.0;
    double openShallowDepth = 0.0;
    std::vector<SeaTransect> transects;
    SeaSpread breakDepth;
    SeaSpread breakDistance;
    SeaSpread slopeDegrees;
    SeaSpread riseDegrees;
    SeaSpread trenchDepth; // the deepest point of each profile within trenchReach
    // The roughness of the deep floor: the windows measured and the median of their rms.
    std::size_t hillWindows = 0;
    double hillRms = 0.0;
    double hillRmsP90 = 0.0;
};

// The figures of the seas of field (with its water: Heightfield::withHydrology for the lakes and
// the seas of agent 51). The same figures whatever the number of threads.
[[nodiscard]] SeaStats measureSeas(const Heightfield& field, const SeaStatsParams& params = {});

// The percentiles of values (unordered), count 0 when empty.
[[nodiscard]] SeaSpread spreadOf(std::vector<double> values);

} // namespace terrain
