#pragma once

// The courses of the rivers of the hydrology (phase 11B), internal to terrain_core. A reach is the
// stretch of a river between two points of the river graph of the planet level (the floors of two
// neighboring cells, 8 to 12 km apart). Its course:
//   - follows the lowest way across the relief between them (dynamic programming over a ladder of
//     cross sections, Viterbi): it stays in the valleys and crosses the sills at their lowest;
//   - is smoothed with the ways of the reaches before and after it (a Gaussian over the
//     sections): natural curves, no angle at the points of the graph, and a tributary ends on
//     the course of the river it joins;
//   - carries the water level from the levels of the graph at both ends, never rising and never
//     above the relief (the river cuts its bed through the sills), with its falls where the relief
//     drops at once;
//   - and the shape of its valley along it: channel and floodplain with meanders in the plains, a
//     U-shaped valley under the mountains, a narrow gorge where it cuts deep into high ground.
// Pure arithmetic on the heights of the relief: the same course on every platform and in every
// thread, whenever it is computed.

#include "fine_lake.hpp"
#include "terrain/generation/heightfield.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace terrain::water {

// The ladder of the lowest way: sections every sectionStep meters along the straight line, each
// with candidates every lateralStep meters across it, up to halfWidth of them on each side; the
// way moves at most one candidate aside from a section to the next and minimizes the sum of the
// heights of its points plus bend meters per meter it moves aside.
struct LadderParams {
    double sectionStep = 128.0;
    double lateralStep = 125.0;
    int halfWidth = 12;
    double bend = 0.05;
};

struct PathPoint {
    double x = 0.0;
    double z = 0.0;
};

// The lowest way from (x0, z0) to (x1, z1) (the caller's images): its first point is (x0, z0), its
// last (x1, z1), at least one section between them.
[[nodiscard]] std::vector<PathPoint> lowestPath(const Heightfield& relief, double x0, double z0,
                                                double x1, double z1, const LadderParams& params);

// The banks of the channel rise kBankSlope meters per meter from its water (river_profile.hpp).
inline constexpr double kBankSlope = 0.3;
// The valley of a course never reaches farther than kReach meters from its axis: it rises to the
// relief over the kReachFade meters before (river_profile.hpp).
inline constexpr double kReach = 6'000.0;
inline constexpr double kReachFade = 1'500.0;
// Where the relief lies below the floor of a valley, the floodplain is filled up to it, kLevee
// meters at most (the levees of the rivers), fading out kLeveeFade meters up the walls: the water
// never stands high above the ground beside its channel (river_profile.hpp).
inline constexpr double kLevee = 5.0;
inline constexpr double kLeveeFade = 40.0;
// The walls of a valley stand up to wallWarpOf(floorHalf) meters nearer or farther than the edge
// of its floor (a noise: spurs and re-entrants, river_profile.hpp).
[[nodiscard]] inline double wallWarpOf(double floorHalf) {
    return 0.35 * floorHalf + 80.0;
}

// The banks of a channel are searched kBankScan meters beyond its edge: its water never stands
// above the lowest relief across it there (lowestAcross).
inline constexpr double kBankScan = 40.0;

// The surface the rivers flow on at (x, z) over relief ground there: the relief, or the water of
// a lake over it (the water of a river never drops into a lake).
using Surface = std::function<float(double x, double z, float ground)>;

// Lowers ground[i] to the lowest surface across the axis at its point i: at half and all of
// half[i] + kBankScan meters on each side, across the direction of the axis there.
void lowestAcross(const Heightfield& relief, const Surface& surface,
                  const std::vector<PathPoint>& axis, const std::vector<double>& half,
                  std::vector<float>& ground);

// One end of a reach: its point of the graph, the water level, the river there and the slope of
// its levels around that point (meters per meter).
struct ReachEnd {
    double x = 0.0;
    double z = 0.0;
    float level = 0.0f;
    float discharge = 0.0f; // m^3/s
    float width = 0.0f;     // meters
    float depth = 0.0f;     // meters
    float slope = 0.0f;
    double phase = 0.0;    // of the meanders (half waves), continuous along the river segment
    double distance = 0.0; // meters from the start of the river segment along the graph
};

struct Course;

// What a course is made of. The ways before and after are in their own images: they are moved to
// meet this one at its ends.
struct ReachInput {
    ReachEnd a;
    ReachEnd b;
    const std::vector<PathPoint>* own = nullptr;
    const std::vector<PathPoint>* before = nullptr; // the way ending at a, or null (a source)
    const std::vector<PathPoint>* after = nullptr;  // the way starting at b, or null (a mouth)
    // A tributary ends on the course of the river it joins, at this point of it, instead of its
    // own smoothed end; or before, where its channel meets the channel of that course (joins, in
    // the images of this reach moved by joinsX and joinsZ), at its water there.
    std::optional<PathPoint> endAt;
    const Course* joins = nullptr;
    double joinsX = 0.0;
    double joinsZ = 0.0;
    // The meanders fade out over the first and the last meanderTaper meters of the river segment
    // when it starts or ends at a confluence, a lake or the sea.
    double segmentLength = 0.0;
    bool taperStart = false;
    bool taperEnd = false;
    Surface surface; // the relief alone when empty
};

// A point of a course: the axis of its valley, smoothed, and what the valley is like there.
struct CoursePoint {
    double x = 0.0;
    double z = 0.0;
    double s = 0.0;           // meters from the start of the course along the axis
    float level = 0.0f;       // the water surface
    float ground = 0.0f;      // the relief on the axis
    float half = 0.0f;        // half the width of the channel
    float depth = 0.0f;       // of the channel
    float floorHalf = 0.0f;   // half the width of the floor of the valley (floodplain, U floor)
    float floorSlope = 0.0f;  // the rise of the floor across the valley (meters per meter)
    float wall = 0.0f;        // the slope of the walls above it (meters per meter)
    float foot = 0.0f;        // meters over which the walls round off at their foot
    float meander = 0.0f;     // amplitude of the meanders of the channel about the axis
    double phase = 0.0;       // of the meanders: the coordinate of their noise (half waves)
    float phaseRate = 0.0f;   // half waves per meter along the axis
    float gorge = 0.0f;       // [0, 1]: from the open valley to the narrow gorge
    float mountain = 0.0f;    // [0, 1]: from the plains to the mountains
};

// Groups of consecutive segments of a course, for the bounds of the search.
struct CourseChunk {
    double x = 0.0; // center
    double z = 0.0;
    double radius = 0.0;  // of the points around the center
    double flat = 0.0;    // farthest reach of the floor and the channel from the axis
    float minLevel = 0.0f;
    float maxDepth = 0.0f;
    float minWall = 0.0f; // the gentlest slope of the walls and of the banks
    std::uint32_t first = 0; // points first to last, segments between them
    std::uint32_t last = 0;
};

struct Course {
    std::vector<CoursePoint> points;
    std::vector<CourseChunk> chunks;
    double minX = 0.0; // the box of the points, widened by the reach of the valley
    double minZ = 0.0;
    double maxX = 0.0;
    double maxZ = 0.0;
    float deepestCut = 0.0f; // the relief above the water on the axis, meters
    float steepest = 0.0f;   // the greatest drop of the water between two points (a fall), m
    double crest = 0.0;      // meters along the axis to its highest relief (a sill it cuts)
    // Where its water stands above the relief of the axis (a hollow before the end of the reach,
    // whose level holds it): the ponds the relief holds there, in the images of the course.
    std::vector<FineLake> ponds;
};

// The course of a reach.
[[nodiscard]] Course buildCourse(const Heightfield& relief, const ReachInput& input);

// The water of course c at the point of its axis nearest to (x, z) (the images of c).
[[nodiscard]] float levelAt(const Course& c, double x, double z);

// The axis of a course: the way own smoothed with the ways before and after it (binomial weights
// over 8 sections on each side, about a Gaussian of 2), its ends kept in place where there is no
// way beyond them. before and after may be null.
[[nodiscard]] std::vector<PathPoint> smoothAxis(const std::vector<PathPoint>* before,
                                                const std::vector<PathPoint>& own,
                                                const std::vector<PathPoint>* after);

// Where the course of a river passes at the point of the graph joining the way upstream (moved
// to end there) to the way downstream, in the images of downstream: where a tributary meets it.
[[nodiscard]] PathPoint smoothedJoint(const std::vector<PathPoint>& upstream,
                                      const std::vector<PathPoint>& downstream);

} // namespace terrain::water
