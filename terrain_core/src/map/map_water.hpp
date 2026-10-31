#pragma once

#include "terrain/map/map_render.hpp"

#include <span>
#include <vector>

namespace terrain {

class Hydrology;
class PlanetLevel;

// Internal to the dev map (map_source.cpp): the courses of the rivers and the marks of the falls
// laid over the samples of a frame.
namespace map_water {

inline constexpr double kWideChannelPixels = 3.0;

// A course to draw: its points (meters, any image, each next one in the image following the
// previous one), the half width of the channel at each and the amplitude of its meanders about
// this axis, and its discharge.
struct Course {
    std::vector<double> xs;
    std::vector<double> zs;
    std::vector<float> halfWidths;
    std::vector<float> meanders;
    float discharge = 0.0f;
};

// A fall: its lip and its drop.
struct Fall {
    double x = 0.0;
    double z = 0.0;
    float drop = 0.0f;
};

// Lays the courses over the samples of frame (with its border, mapGridSize) where their channel is
// narrower than kWideChannelPixels: each sample takes the part of its pixel the nearest course
// covers, the channel at least leastPixels wide, and the discharge of the course covering it most
// (the largest discharge among equals). Wider, the samples show the channel itself (its meanders
// about the axis of the course): the river samples within its meanders take its discharge only.
// The same samples whatever the order of the courses.
void drawCourses(const MapFrame& frame, std::span<const Course> courses, double leastPixels,
                 std::span<MapSample> out);

// Marks the falls of at least leastDrop meters: their center within centerPixels of the lip, a
// ring out to ringPixels; the largest drop where marks meet.
void drawFalls(const MapFrame& frame, std::span<const Fall> falls, double centerPixels,
               double ringPixels, double leastDrop, std::span<MapSample> out);

// The courses of the reaches whose course may cross the box of frame, and their falls, the steps
// of a reach whose lips lie within stretch meters of the next taken as one fall (its first lip,
// the whole drop): one reachesNear and one reach per reach, counted in stats.hydrologyCalls.
void reachCourses(const Hydrology& hydrology, const MapFrame& frame, double stretch,
                  std::vector<Course>& courses, std::vector<Fall>& falls, MapSampleStats& stats);

// The pieces of the rivers of the planet level of at least minDischarge m^3/s whose cell lies in
// the box of frame (or a cell around it), and its waterfalls of at least leastDrop meters in the
// box. Never the Hydrology.
void planetCourses(const PlanetLevel& planet, const MapFrame& frame, double minDischarge,
                   double leastDrop, std::vector<Course>& courses, std::vector<Fall>& falls);

} // namespace map_water

} // namespace terrain
