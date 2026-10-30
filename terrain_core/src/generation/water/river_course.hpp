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

#include <array>
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
    // own smoothed end; or before, where its channel meets the channel of that river, before the
    // confluence or after it (joins: their courses, in the images of this reach moved by x and
    // z), at its water there.
    std::optional<PathPoint> endAt;
    // The surface of its valley at (x, z) of this reach over relief ground there (infinity where
    // it lies above ground + kLevee): the tributary never stands above it.
    struct Join {
        const Course* course = nullptr;
        double x = 0.0;
        double z = 0.0;
        std::function<double(double x, double z, double ground)> surface;
    };
    std::array<Join, 2> joins{};
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
    // The level the valley is carved from: the water, or above the steps of a fall the slope it
    // falls down (the valley follows the slope, the water falls in steps in a slot in its floor,
    // under the slope enough for the slot to stay cut into the floor at their lips too,
    // stepFalls).
    float valley = 0.0f;
    float ground = 0.0f;      // the lowest relief across the channel (or the valley it joins)
    float top = 0.0f;         // the relief on the axis: how deep the valley cuts
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
    // On a stretch in steps, the walls of the slot its water falls down in reach about this far
    // beyond the channel (river_slot.hpp); 0 elsewhere.
    float slot = 0.0f;
    bool fall = false;        // on a stretch where the water falls in steps (river_falls.hpp)
    // In a corridor of falls (river_winding.hpp), or within kWindWindow meters of the segments
    // around the point: the middle of its slot lies shift meters left of the axis at the point,
    // never more than windMost meters aside anywhere in its corridor; 0 elsewhere.
    float shift = 0.0f;
    float windMost = 0.0f;
    // Below a drop (river_slot.hpp): the bowls deepen the channel of the segment from the point to
    // the next by bowlDeep meters at most and widen it by bowlWide of itself, kBowlBeyond meters
    // beyond its ends too; 0 elsewhere.
    float bowlDeep = 0.0f;
    float bowlWide = 0.0f;
};

// The winding of the slot of a corridor of falls (river_winding.hpp), from to to meters along the
// course: two noises of waves meters (wave, phase, amplitude and seed each), faded in and out over
// fade meters at its ends, a share of it kept toward each side (the water kept in the rock),
// never more than most meters aside (as measured, with a margin). Drawn from table: its offset and
// rate every step meters from from (interleaved), cubic between them.
struct SlotWinding {
    double from = 0.0;
    double to = 0.0;
    double fade = 1.0;
    std::array<double, 2> wave{1.0, 1.0};
    std::array<double, 2> phase{};
    std::array<double, 2> amplitude{};
    std::array<std::uint64_t, 2> seed{};
    double left = 1.0;
    double right = 1.0;
    double most = 0.0;
    double step = 1.0;
    std::vector<double> table;
};

// What bounds the windings of a course (river_winding.cpp: windBounds), every kBoundBin meters
// from from: the least and the most its slot lies left of its axis and the most its frame
// stretches (interleaved, with a margin), in levels[0]; the same over 2, 4, 8... bins from each
// in the next levels.
struct WindTable {
    double from = 0.0;
    std::vector<std::vector<float>> levels;
};

// A pool of a stretch in steps (river_slot.hpp): below the drop of drop meters whose face comes
// down from lip to foot meters along the course, up to the next lip at next.
struct FallPool {
    double lip = 0.0;
    double foot = 0.0;
    double next = 0.0;
    float drop = 0.0f;
    double length = 0.0; // its bowl reaches length meters below the foot, deep meters deep at most
    double deep = 0.0;
};

// A block of rock in a pool of a fall (river_slot.hpp): radius meters wide around (x, z), its top
// at top, flatter there and steeper at its sides than a dome (falling with the cube of the
// distance), its foot at base (below the bed).
struct FallBlock {
    double x = 0.0;
    double z = 0.0;
    float radius = 0.0f;
    float top = 0.0f;
    float base = 0.0f;
};

// The lip of a drop of a stretch in steps (river_lips.hpp): where its face starts on the axis
// (point of the course, s along it), the course going down (dirX, dirZ) there; the water above and
// below it; how much harder the rock of the lip is than that under it (contrast, in [0, 1]); and
// its edge across the slot: its middle recedes upstream by recess meters, over width meters on
// each side of the axis, never by less than edge meters there (an irregular edge, drawn by seed),
// and turn meters more per meter across it (an edge at an angle to the slot), the valley moved
// downstream by as much and back over reach meters below the lip. Its point is on the axis of the
// course (the middle of the slot lies aside of it where it winds); its side in the frame of the
// slot (river_winding.hpp).
struct FallLip {
    double x = 0.0;
    double z = 0.0;
    double dirX = 1.0;
    double dirZ = 0.0;
    double s = 0.0;
    std::uint32_t point = 0;
    float top = 0.0f;
    float bottom = 0.0f;
    float contrast = 0.0f;
    float recess = 0.0f;
    float edge = 0.0f;
    float width = 0.0f;
    float reach = 0.0f;
    float turn = 0.0f;
    std::uint64_t seed = 0;
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
    // The rise of the water and of the valley of each segment beyond its ends (risesOf).
    std::vector<float> rises;
    std::vector<float> valleyRises;
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
    // The noise of the slots of its falls, and the blocks of rock in their pools with their box
    // (river_slot.hpp).
    std::uint64_t slotSeed = 0;
    std::vector<FallBlock> blocks;
    double blocksMinX = 0.0;
    double blocksMinZ = 0.0;
    double blocksMaxX = 0.0;
    double blocksMaxZ = 0.0;
    // The lips of the drops of its falls and the box of what they move (river_lips.hpp).
    std::vector<FallLip> lips;
    double lipsMinX = 0.0;
    double lipsMinZ = 0.0;
    double lipsMaxX = 0.0;
    double lipsMaxZ = 0.0;
    // The windings of the slots of its corridors (in order along it) and the pools of its falls
    // (river_winding.hpp, river_slot.hpp).
    std::vector<SlotWinding> windings;
    WindTable windTable;
    std::vector<FallPool> pools;
    // For each of its points, the last pool whose lip lies at it or before it (-1: none).
    std::vector<std::int32_t> poolOf;
    // Where its slots wind, the points of its water: those of the course, its corridors every few
    // meters, at the middle of the slot; lineAxis where each lies on the axis. Empty without them.
    std::vector<CoursePoint> line;
    std::vector<PathPoint> lineAxis;
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
