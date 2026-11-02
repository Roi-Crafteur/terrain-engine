// The pawn of the game by the faces of the coasts in three dimensions (agent 33), on the real
// collision of the tiles (the LOD 0 meshes a client takes it from), at the places of
// coast_rock_support.hpp: never in the rock, never through the ground.
// - The teleports (Bridge/ground_read: Terrain.Go, the load after a teleport): on every column
//   kGridStep meters apart within kGridHalf meters of each face, the pawn placed as
//   PollTerrainGround places it (its lowest point kLoadClearance meters over the level read: the
//   top of the rock of the tiles, or the water over it) has its axis in the air from its feet to
//   its head, and its capsule is clear of the rock or, touching a face by its side, clear once
//   moved by its radius at most (the first move of Unreal pushes it out of the wall). Before
//   ground_read read the top of the rock, the heightmap put it up to 50 m into the rock behind
//   the faces.
// - The landings (Terrain.Pawn: UTerrainDevTools::FindGameGround, and a click on the dev map,
//   clickLandingOn): from every column kViewStep meters apart (kFjordViewStep by the fjord, where
//   no ground within the search holds the pawn: TooSteep), dry or not, the
//   candidates of walkableCandidates on the top of the rock, each checked by five rays down onto
//   the collision (hitting the back of the triangles too) and checkStand; the first one that
//   stands, the capsule put kStandClearance meters over the highest hit, is clear of the rock.
//   Every view finds one within the 50 m of the search but by the fjord, whose bank is the steep
//   wall of the relief (none there: TooSteep), and none is refused by the collision alone.
// - The walks (kStep meters, steps of kStepMost meters at most, the capsule clear wherever it
//   stands): on the platform of the notched cliff into its notch (kNotchWalk meters past the
//   plane of the face at least, under its ceiling), along the foot of its face, and on the top
//   of each face out to its edge, where the next floor lies a face below: no hole on the way.
// The arches and the sea caves come with the tunnels, in their own commit.
#include "coast_planet_support.hpp"
#include "coast_rock_support.hpp"

#include "map_landing.hpp"
#include "place_pose.hpp"
#include "walkable_ground.hpp"

#include "ground_read.hpp"

#include "terrain/generation/coast.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using coast_rock::Capsule;
using coast_rock::Hit;
using coast_rock::Place;
using coast_rock::Tiles;

constexpr double kGridHalf = 24.0;
constexpr double kGridStep = 2.0;
constexpr double kViewStep = 4.0;
constexpr double kFjordViewStep = 12.0;
constexpr double kLoadClearance = 0.5;   // LoadClearanceCm of TerrainWorldLoadingPhase.cpp
constexpr double kStandClearance = 0.02; // ClearanceCm of TerrainDevPawn.cpp
constexpr double kRayReach = 20.0;       // RayReachCm
constexpr double kWaterAround = 3.0;     // WaterAroundMeters
constexpr double kStep = 0.1;
constexpr double kStepMost = 0.45;
constexpr double kNotchWalk = 1.0;
const double kWalkable = terrain::unreal::walkableSlope(44.765, 0.0);

constexpr std::array<Place, 5> kPlaces{coast_rock::kCalanque, coast_rock::kCliff,
                                       coast_rock::kRocky, coast_rock::kFjord,
                                       coast_rock::kStack};

// The default world with its planet file (coast_planet_support.hpp).
const terrain::TerrainTiles& world() {
    return coast_planet::tiles();
}

// Whether capsule c, its lowest point at bottom over (x, z), is clear of the rock where it is or
// moved by its radius at most in one of 8 directions.
bool freed(Tiles& tiles, double x, double z, double bottom, const Capsule& c) {
    if (coast_rock::clearOfRock(tiles, x, z, bottom, c)) {
        return true;
    }
    for (int k = 0; k < 8; ++k) {
        const double a = k * 3.14159265358979323846 / 4.0;
        if (coast_rock::clearOfRock(tiles, x + c.radius * std::cos(a), z + c.radius * std::sin(a),
                                    bottom, c)) {
            return true;
        }
    }
    return false;
}

void testTeleports() {
    const Capsule capsule;
    std::size_t columns = 0, axisInRock = 0, touching = 0, stuck = 0;
    for (const Place& p : kPlaces) {
        Tiles tiles(world(), 0);
        for (double dz = -kGridHalf; dz <= kGridHalf; dz += kGridStep) {
            for (double dx = -kGridHalf; dx <= kGridHalf; dx += kGridStep) {
                const terrain::unreal::GroundLevel level =
                    terrain::unreal::groundLevelAt(world(), p.x + dx, p.z + dz);
                const double bottom = level.stand + kLoadClearance;
                ++columns;
                axisInRock += coast_rock::lineInRock(tiles, level.x, level.z, bottom,
                                                     bottom + capsule.height)
                                  ? 1
                                  : 0;
                const bool clear =
                    coast_rock::clearOfRock(tiles, level.x, level.z, bottom, capsule);
                touching += clear ? 0 : 1;
                stuck += clear || freed(tiles, level.x, level.z, bottom, capsule) ? 0 : 1;
            }
        }
    }
    std::printf("the teleports by the faces: %zu columns, the axis of the pawn in the rock at %zu, "
                "its side touching a face at %zu, held in the rock at %zu\n",
                columns, axisInRock, touching, stuck);
    TEST_CHECK(columns >= 3'000);
    TEST_CHECK(axisInRock == 0 && stuck == 0);
}

// A ray down from top onto the collision at (x, z) over kRayReach * 2 meters: the first surface
// it meets, either side of its triangles.
terrain::unreal::FloorProbe rayDown(Tiles& tiles, double x, double z, double top) {
    terrain::unreal::FloorProbe probe;
    for (const Hit& h : tiles.hitsAt(x, z)) {
        if (h.y <= top && h.y >= top - 2.0 * kRayReach && (!probe.hit || h.y > probe.altitude)) {
            probe = {true, h.y, std::abs(h.up)};
        }
    }
    return probe;
}

// Where FindGameGround puts the pawn of the game from a view over (x, z): the altitude of its
// lowest point and its column (stands), or why it puts it nowhere: no candidate on the top of the
// rock (too steep or wet all around: reason), or every candidate refused by the collision.
struct Landing {
    bool stands = false;
    terrain::unreal::WalkableReason reason = terrain::unreal::WalkableReason::Found;
    double x = 0.0;
    double z = 0.0;
    double bottom = 0.0;
};

Landing findGameGround(Tiles& tiles, double x, double z, const Capsule& c) {
    const terrain::Heightfield& field = world().heightfield();
    terrain::unreal::WalkableSearch search;
    search.maxSlope = terrain::unreal::walkableSlope(44.765, 5.0);
    terrain::unreal::StandLimits limits;
    limits.minNormalUp = terrain::unreal::walkableNormalUp(44.765, 5.0);
    const terrain::unreal::GroundHeight height = [](double px, double pz) {
        return static_cast<double>(world().groundAt(px, pz));
    };
    const terrain::unreal::GroundWet wet = [&field](double px, double pz) {
        return terrain::unreal::columnHasWater(field.sampleAt(px, pz));
    };
    const terrain::unreal::GroundSample sample = [&field](double px, double pz) {
        return field.sampleAt(px, pz);
    };
    const terrain::unreal::WalkableCandidates candidates =
        terrain::unreal::walkableCandidates(x, z, search, height, wet, 64);
    for (const terrain::unreal::WalkableSpot& s : candidates.spots) {
        const std::array<std::array<double, 2>, 5> around{
            {{0.0, 0.0}, {c.radius, 0.0}, {-c.radius, 0.0}, {0.0, c.radius}, {0.0, -c.radius}}};
        std::array<terrain::unreal::FloorProbe, 5> probes{};
        for (std::size_t k = 0; k < around.size(); ++k) {
            probes[k] =
                rayDown(tiles, s.x + around[k][0], s.z + around[k][1], s.ground + kRayReach);
        }
        const terrain::unreal::StandCheck check = terrain::unreal::checkStand(
            probes, s.ground, terrain::unreal::waterSurfaceAround(s.x, s.z, kWaterAround, sample),
            limits);
        if (check.refusal == terrain::unreal::StandRefusal::None) {
            return {true, candidates.reason, s.x, s.z, check.altitude + kStandClearance};
        }
    }
    return {false, candidates.reason, x, z, 0.0};
}

void testLandings() {
    const Capsule capsule;
    std::size_t views = 0, dry = 0, landed = 0, stuck = 0, overWater = 0, steep = 0, refused = 0;
    for (const Place& p : kPlaces) {
        Tiles tiles(world(), 0);
        // By the fjord every view searches the whole 50 m in vain: fewer of them.
        const bool fjord = p.x == coast_rock::kFjord.x && p.z == coast_rock::kFjord.z;
        const double step = fjord ? kFjordViewStep : kViewStep;
        for (double dz = -kGridHalf; dz <= kGridHalf; dz += step) {
            for (double dx = -kGridHalf; dx <= kGridHalf; dx += step) {
                ++views;
                const terrain::unreal::MapLanding click =
                    terrain::unreal::clickLandingOn(world().heightfield(), p.x + dx, p.z + dz,
                                                    terrain::unreal::WalkableSearch{}.radius);
                dry += click.clickWet ? 0 : 1;
                if (click.kind != terrain::unreal::MapLandingKind::Land) {
                    ++overWater;
                    continue;
                }
                const Landing l = findGameGround(tiles, click.x, click.z, capsule);
                landed += l.stands ? 1 : 0;
                steep += !l.stands && l.reason == terrain::unreal::WalkableReason::TooSteep ? 1 : 0;
                refused += !l.stands && l.reason == terrain::unreal::WalkableReason::Found ? 1 : 0;
                const bool clear =
                    l.stands && coast_rock::clearOfRock(tiles, l.x, l.z, l.bottom, capsule);
                stuck += l.stands && !clear ? 1 : 0;
            }
        }
    }
    std::printf("the landings by the faces: %zu views (%zu over dry ground), %zu over the water "
                "alone, %zu landed, %zu of them in the rock; %zu with no walkable ground within "
                "the search (the wall of the fjord), %zu whose candidates the collision refused\n",
                views, dry, overWater, landed, stuck, steep, refused);
    TEST_CHECK(dry >= 300 && landed >= 500 && stuck == 0);
    TEST_CHECK(refused == 0 && landed + overWater + steep == views);
}

// A walker over the collision: the floor it stands on, the next one along its way within a
// step (the highest under floor + kStepMost), and the air over it.
struct Walk {
    double walked = 0.0;
    double steepest = 0.0;
    std::size_t holes = 0;
    bool blocked = false; // rock ahead: a step up or a ceiling too low for the capsule
    bool drop = false;    // the next floor more than a step down: an edge
    double at = 0.0;      // where it stopped, along its way from its start
};

// The lowest point of capsule c standing at (x, z) on a floor: over the highest of the floors
// under its middle and four points of its side (as ProbeSpot), kStandClearance above.
double standing(Tiles& tiles, double x, double z, double floor, const Capsule& c) {
    double highest = floor;
    for (const auto& [ox, oz] : std::array<std::array<double, 2>, 4>{
             {{c.radius, 0.0}, {-c.radius, 0.0}, {0.0, c.radius}, {0.0, -c.radius}}}) {
        const terrain::unreal::FloorProbe p = rayDown(tiles, x + ox, z + oz, floor + kStepMost);
        highest = p.hit ? std::max(highest, p.altitude) : highest;
    }
    return highest + kStandClearance;
}

Walk walk(Tiles& tiles, double x, double z, double dx, double dz, double length, const Capsule& c) {
    Walk w;
    double floor = tiles.topAt(x, z);
    for (double t = kStep; t <= length; t += kStep) {
        const double px = x + dx * t;
        const double pz = z + dz * t;
        const std::vector<Hit> hits = tiles.hitsAt(px, pz);
        if (hits.empty()) {
            ++w.holes;
            break;
        }
        const Hit* next = nullptr;
        for (const Hit& h : hits) {
            next = h.up > 0.0 && h.y <= floor + kStepMost && (next == nullptr || h.y > next->y)
                       ? &h
                       : next;
        }
        // An edge: the next floor more than a step down, or going down steeper than the walkable
        // angle (the pawn slides off it).
        w.blocked = next == nullptr;
        w.drop = next != nullptr &&
                 (next->y < floor - kStepMost || floor - next->y > kWalkable * kStep);
        if (w.blocked || w.drop) {
            break;
        }
        if (!coast_rock::clearOfRock(tiles, px, pz, standing(tiles, px, pz, next->y, c), c)) {
            w.blocked = true;
            break;
        }
        w.steepest = std::max(w.steepest, std::abs(next->y - floor));
        floor = next->y;
        w.walked = t;
    }
    w.at = w.walked;
    return w;
}

// The walks by a face: on its platform into its notch (notched), along its foot, on its top out
// to its edge. The line of a face: the plane at its middle, the air along (ax, az). (The top of
// the wall of the fjord is the steep wall of the relief: no walk there.)
void walksBy(const Place& p, bool notched, std::size_t& holes, double& steepest) {
    const coast_rock::Face f = coast_rock::faceAt(world().heightfield(), p);
    const double ax = static_cast<double>(f.face.airX);
    const double az = static_cast<double>(f.face.airZ);
    const Capsule capsule;
    Tiles tiles(world(), 0);
    const Walk top = walk(tiles, f.x - 20.0 * ax, f.z - 20.0 * az, ax, az, 30.0, capsule);
    std::printf("%s: on its top toward the sea, %.1f m walked to %s %+.1f m from the plane of the "
                "face, steps of %.2f m at most, %zu holes\n",
                p.name, top.walked, top.drop ? "its edge" : "a stop", top.at - 20.0, top.steepest,
                top.holes);
    TEST_CHECK(top.drop && std::abs(top.at - 20.0) <= 3.0);
    holes += top.holes;
    steepest = std::max(steepest, top.steepest);
    if (!notched) {
        return;
    }
    const Walk in = walk(tiles, f.x + 15.0 * ax, f.z + 15.0 * az, -ax, -az, 25.0, capsule);
    const Walk along =
        walk(tiles, f.x + 1.5 * ax - 15.0 * az, f.z + 1.5 * az + 15.0 * ax, az, -ax, 30.0, capsule);
    std::printf("%s: from its platform toward the face, %.1f m walked, %.1f m into the notch "
                "behind the plane of the face (%s), steps of %.2f m at most, %zu holes; along its "
                "foot %.1f m walked%s, steps of %.2f m at most, %zu holes\n",
                p.name, in.walked, in.walked - 15.0, in.blocked ? "stopped by the rock" : "no stop",
                in.steepest, in.holes, along.walked, along.blocked ? " to the rock" : "",
                along.steepest, along.holes);
    TEST_CHECK(in.blocked && in.walked - 15.0 >= kNotchWalk);
    TEST_CHECK(along.walked >= 10.0);
    holes += in.holes + along.holes;
    steepest = std::max({steepest, in.steepest, along.steepest});
}

// The walk on the shoulder of the stack, from kShoulder of its radius out to its edge (its dome
// is too steep to walk down).
constexpr double kShoulder = 0.6;

void walkStack(std::size_t& holes, double& steepest) {
    const Place& p = coast_rock::kStack;
    const std::vector<terrain::CoastStack> stacks =
        world().heightfield().stacksIn(p.x - 50.0, p.z - 50.0, p.x + 50.0, p.z + 50.0);
    TEST_CHECK(!stacks.empty());
    if (stacks.empty()) {
        return;
    }
    const auto lower = [](const terrain::CoastStack& a, const terrain::CoastStack& b) {
        return a.height < b.height;
    };
    const terrain::CoastStack& s = *std::max_element(stacks.begin(), stacks.end(), lower);
    Tiles tiles(world(), 0);
    const double from = kShoulder * static_cast<double>(s.radius);
    const Walk w = walk(tiles, s.x + from, s.z, 1.0, 0.0, 40.0, Capsule{});
    std::printf("%s: on its shoulder from %.1f m off its middle, %.1f m walked to %s, steps of "
                "%.2f m at most, %zu holes\n",
                p.name, from, w.walked, w.drop ? "its edge" : "a stop", w.steepest, w.holes);
    TEST_CHECK(w.drop && w.walked >= 3.0);
    holes += w.holes;
    steepest = std::max(steepest, w.steepest);
}

void testWalks() {
    std::size_t holes = 0;
    double steepest = 0.0;
    walksBy(coast_rock::kCalanque, false, holes, steepest);
    walksBy(coast_rock::kCliff, true, holes, steepest);
    walksBy(coast_rock::kRocky, false, holes, steepest);
    walkStack(holes, steepest);
    TEST_CHECK(holes == 0 && steepest <= kStepMost);
}

} // namespace

int main() {
    testTeleports();
    testLandings();
    testWalks();
    return test::result();
}
