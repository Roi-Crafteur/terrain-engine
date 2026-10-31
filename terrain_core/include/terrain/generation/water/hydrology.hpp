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
// the Heightfield of the world (Heightfield::withHydrology) keeps every bit of the relief there,
// but in the band of the coasts (agent 33), where the coastal relief fades out toward the relief
// without the coasts as the valleys come near (Hydrology::clearance).
// The lakes are flat at the levels of the planet level (their spill level, or lower where the
// climate dries them: salt lakes), over their cells and those around them.
//
// Deterministic: the courses are pure functions of the relief and of the planet level, the same
// bits whatever the thread or the order in which they are computed. Thread-safe.

#include "terrain/generation/climate.hpp"
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
    bool carved = false; // a valley lies below the relief (raised by raise)
    double height = 0.0; // its surface, before the rock of its walls
    // The surface of the valley of another river (not the reaches just before and after on the
    // same one): where two valleys meet, the rock of their walls fades out.
    double second = std::numeric_limits<double>::infinity();
    bool wet = false;     // in its channel
    float level = 0.0f;   // the water there
    // The level the floor of the valley is carved from: the water, or beside the steps of a fall
    // the slope it falls down (continuous along the river where its water falls).
    float floor = 0.0f;
    float gorge = 0.0f;   // [0, 1], open valley to narrow gorge
    float mountain = 0.0f; // [0, 1], plains to mountains
    double upX = 1.0;     // unit direction away from the river
    double upZ = 0.0;
    double slope = 0.0;   // of the surface across the valley (meters per meter)
    double above = 0.0;   // height of the walls above the floor of the valley
    // The relief filled up to the floor of a valley where it lies below it (the levees of the
    // rivers), 5 meters at most.
    double raise = 0.0;
    // The water of a pond of a river over the point (where the relief lies below the water of
    // the river and holds it), or -infinity.
    float pond = -std::numeric_limits<float>::infinity();
    // The banks of a channel beside the point hold the ground up to this height, or -infinity:
    // the water never stands above the ground beside it, whatever valley carves there.
    double bank = -std::numeric_limits<double>::infinity();
    // The reach whose valley it is (Hydrology::reach), or -1.
    std::size_t reach = static_cast<std::size_t>(-1);
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
    // The level the valley is carved from: the water, or above the steps of a fall the slope they
    // come down, raised so that their slot stays cut into the floor at their lips too (its floor
    // stands kFloorRise, 0.6 m, above it).
    float valley = 0.0f;
};

// A fall: the stretch of a course where its water drops at once.
struct RiverFall {
    double x = 0.0; // the lip
    double z = 0.0;
    float top = 0.0f;    // the water above
    float bottom = 0.0f; // the water below
    float width = 0.0f;
    double footX = 0.0; // the foot: the point of the course where the water below begins
    double footZ = 0.0;
};

// A block of rock in a pool of a fall: a dome radius meters wide around (x, z), its top at top.
struct RiverBlock {
    double x = 0.0; // meters, in the images of the reach
    double z = 0.0;
    float radius = 0.0f;
    float top = 0.0f;
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
    std::vector<RiverBlock> blocks;
};

class Hydrology {
public:
    // The water of the planet level over relief (a Heightfield without hydrology, the one the
    // planet level was built from), seed drawing its meanders. Throws std::invalid_argument if
    // they do not tile the same world. The courses, the falls and the lakes are found over the
    // relief without its coasts (Heightfield::withoutCoasts, as the planet level): relief()
    // returns that one.
    Hydrology(std::uint64_t seed, const Heightfield& relief,
              std::shared_ptr<const PlanetLevel> planet, const HydrologyParams& params = {});
    ~Hydrology();
    Hydrology(const Hydrology&) = delete;
    Hydrology& operator=(const Hydrology&) = delete;

    [[nodiscard]] const PlanetLevel& planet() const;
    [[nodiscard]] const Heightfield& relief() const;
    [[nodiscard]] const HydrologyParams& params() const;

    // What the queries of the points of a box share (a batch of Heightfield::samplesAt or
    // heightsAt): the reaches and the lakes that may reach it, found once with what of their
    // courses may carve it, and the memo of the noise of the walls of the valleys. Used by one
    // thread at a time; the queries give the same bits with it as without it.
    class Region {
    public:
        Region();
        ~Region();
        Region(Region&&) noexcept;
        Region& operator=(Region&&) noexcept;
        Region(const Region&) = delete;
        Region& operator=(const Region&) = delete;

        struct Data; // hydrology_impl.hpp

    private:
        friend class Hydrology;
        std::unique_ptr<Data> data_;
    };
    // The region of the canonical box [minX, maxX] x [minZ, maxZ] where the relief lies between
    // groundMin and groundMax: empty (the queries go point by point) unless the box lies in one
    // cell of the planet level.
    [[nodiscard]] Region regionOf(double minX, double minZ, double maxX, double maxZ,
                                  double groundMin, double groundMax) const;

    // The valleys at the canonical (x, z) where the relief is ground meters high. region: one
    // whose box and relief hold the point and its ground, or null.
    [[nodiscard]] RiverCarve riversAt(double x, double z, double ground,
                                      Region* region = nullptr) const;
    // How far the valleys, ponds and lakes keep from the canonical (x, z) where the relief is
    // ground meters high, meters (horizontal or vertical, the least of them): where it is above 0,
    // riversAt(x, z, ground) neither carves nor fills nor holds a bank or a pond, and lakeAt holds
    // no water; it falls continuously to 0 toward them (river_clearance.cpp). The coasts (agent 33)
    // fade out with it: the corridors of the rivers keep the relief they were found over.
    [[nodiscard]] double clearance(double x, double z, double ground) const;
    // Whether a reach or a lake may reach the canonical (x, z): where it is false, clearance is
    // infinite whatever the relief (a cheap test of the reaches near and of the lakes around).
    [[nodiscard]] bool mayMatter(double x, double z) const;
    // The lake over the canonical (x, z) where the relief (before the valleys of the rivers) is
    // ground meters high. region: as for riversAt.
    [[nodiscard]] LakeWater lakeAt(double x, double z, double ground,
                                   const Region* region = nullptr) const;
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

// The humidity of the planet level of hydrology (PlanetLevel::humidityAt) as the climate of the
// world reads it (ClimateField), hydrology kept alive by it; nullptr without hydrology.
[[nodiscard]] std::shared_ptr<const HumiditySource>
planetHumidity(std::shared_ptr<const Hydrology> hydrology);

} // namespace terrain
