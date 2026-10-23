#pragma once

// The water of the world (phase 11B): the rivers of the planet level carved into the relief, and
// its lakes. The relief comes first and the water follows it: the river graph of the planet level
// (its cells of 8 km) gives where the rivers of at least HydrologyParams::minDischarge go and the
// level of their water at each point of the graph; every reach between two points of the graph
// then finds its course across the relief itself (river_course.hpp, computed on demand and kept),
// and its valley is carved: a channel and a floodplain with meanders in the plains, a U-shaped
// valley under the mountains (its bed at most PlanetBreachParams::mountainDepth below the relief,
// else a lake held by the sill: the planet level keeps those), a narrow gorge where it cuts deep
// into high ground, its falls where the relief drops at once. Outside the valleys nothing moves:
// the Heightfield of the world (Heightfield::withHydrology) keeps every bit of the relief there.
// The lakes are flat at the levels of the planet level (their spill level, or lower where the
// climate dries them: salt lakes), over their cells and those around them.
//
// Deterministic: the courses are pure functions of the relief and of the planet level, the same
// bits whatever the thread or the order in which they are computed. Thread-safe.

#include "terrain/generation/heightfield.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace terrain {

class PlanetLevel;

struct HydrologyParams {
    // The rivers carved: their discharge at the upstream end of the reach, m^3/s (the planet
    // level must keep them: PlanetRiverParams::minDischarge at most this).
    double minDischarge = 10.0;
};

// What the valleys of the rivers make of a point of the relief (Hydrology::riversAt).
struct RiverCarve {
    bool carved = false; // a valley lies below the relief
    double height = 0.0; // its surface, before the rock of its walls
    // The surface of the valley of another river (not the reaches just before and after on the
    // same one): where two valleys meet, the rock of their walls fades out.
    double second = std::numeric_limits<double>::infinity();
    bool wet = false;     // in its channel, or under a pond on its floor held by a sill ahead
    float level = 0.0f;   // the water there
    float gorge = 0.0f;   // [0, 1], open valley to narrow gorge
    float mountain = 0.0f; // [0, 1], plains to mountains
    double upX = 1.0;     // unit direction away from the river
    double upZ = 0.0;
    double slope = 0.0;   // of the surface across the valley (meters per meter)
    double above = 0.0;   // height of the walls above the floor of the valley
};

// The lake over a point (Hydrology::lakeAt).
struct LakeWater {
    std::int32_t lake = -1; // index in PlanetLevel::lakes(), -1 for none
    float level = 0.0f;
    WaterKind kind = WaterKind::None; // Lake or SaltLake
};

// A point of the course of a river, for the renderers (Unreal splines).
struct RiverPoint {
    double x = 0.0; // meters, the images of the reach (they follow each other across the seams)
    double z = 0.0;
    float level = 0.0f; // the water surface
    float width = 0.0f; // of the channel
    float depth = 0.0f;
    float floorWidth = 0.0f; // of the floor of the valley
    float gorge = 0.0f;      // [0, 1]: open valley to narrow gorge
    float mountain = 0.0f;   // [0, 1]: plains to mountains
    float meander = 0.0f;    // amplitude of the meanders of the channel about this axis
};

// A fall: the stretch of a course where its water drops at once.
struct RiverFall {
    double x = 0.0; // the lip
    double z = 0.0;
    float top = 0.0f;    // the water above
    float bottom = 0.0f; // the water below
    float width = 0.0f;
};

// The course of one reach.
struct RiverReach {
    std::int32_t segment = -1; // in PlanetLevel::rivers()
    // The reach goes from this point of the segment to the next; -1: the outlet of a lake, from
    // the floor of the cell of its sill to the first point of the segment.
    std::int32_t vertex = -1;
    float discharge = 0.0f;
    float deepestCut = 0.0f; // the relief above the water, meters, at most
    std::vector<RiverPoint> points;
    std::vector<RiverFall> falls;
};

class Hydrology {
public:
    // The water of the planet level over relief (a Heightfield without hydrology, the one the
    // planet level was built from), seed drawing its meanders. Throws std::invalid_argument if
    // they do not tile the same world.
    Hydrology(std::uint64_t seed, const Heightfield& relief,
              std::shared_ptr<const PlanetLevel> planet, const HydrologyParams& params = {});
    ~Hydrology();
    Hydrology(const Hydrology&) = delete;
    Hydrology& operator=(const Hydrology&) = delete;

    [[nodiscard]] const PlanetLevel& planet() const;
    [[nodiscard]] const Heightfield& relief() const;
    [[nodiscard]] const HydrologyParams& params() const;

    // The valleys at the canonical (x, z) where the relief is ground meters high.
    [[nodiscard]] RiverCarve riversAt(double x, double z, double ground) const;
    // The lake over the canonical (x, z) where the relief (before the valleys of the rivers) is
    // ground meters high.
    [[nodiscard]] LakeWater lakeAt(double x, double z, double ground) const;
    // The level of the water of a lake of the planet level over the relief (computed on demand,
    // then kept): its level, lower where the relief lets its water out sooner; -infinity where
    // the relief holds none of it.
    [[nodiscard]] float lakeLevel(std::int32_t lake) const;

    // The reaches: one per river cell of the planet level carved, in the order of the cells, then
    // one per outlet of a lake into a river carved, in the order of the cells of their sills.
    [[nodiscard]] std::size_t reachCount() const;
    // Those whose course may come within radius meters of (x, z), by index.
    [[nodiscard]] std::vector<std::size_t> reachesNear(double x, double z, double radius) const;
    // The course of a reach (computed on demand, then kept).
    [[nodiscard]] RiverReach reach(std::size_t index) const;
    // How many courses are computed so far, and the bytes they hold.
    [[nodiscard]] std::size_t coursesComputed() const;
    [[nodiscard]] std::size_t courseBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace terrain
