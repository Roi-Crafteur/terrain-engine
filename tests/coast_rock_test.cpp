// The faces of the coasts in three dimensions (agent 33, coast_faces.hpp) in the streamed tiles
// and on the horizon, on the default world, at the places of coast_rock_support.hpp:
// - Each place holds its face, drawn whole by the tiles of LOD 0 to 3 (its run), kLeastFace
//   meters high at least; the notched cliff and rocky coast a notch at their foot, kNotchDeep
//   meters deep and kNotchHigh high at least (the calanque, which plunges, none).
// - The meshes of the tiles a client streams around each place (LOD 0 to 3, out to 512 m): no
//   opening, no edge of more than two triangles, no inverted triangle, and no piece floating
//   within a tile (a closed piece of rock or air reaching none of its sides).
// - The tiles and the horizon show the same face (Heightfield::seenAt, the heights the grids of
//   the horizon read beyond 512 m): across each face, the steps of the LOD 0 tiles, of the LOD 3
//   tiles (the last ones before the horizon) and of the horizon stand kStepOff and kStepOffLod3
//   meters apart at most (kStepOffPlunge on the horizon for a face that plunges, which rises
//   there from the shore: a pitch of its level 0 further from the sea), their ledges kLedgeAt
//   meters on either side kLedgeOff and kLedgeOffLod3: no jump at 512 m.
// - The notches of LOD 0: a floor kNotchFloor meters over the sea at most and a ceiling over it
//   kNotchRoom meters higher at least (a pawn walks in), over kNotchReach meters at least.
// - The steep faces are bare rock: kBareShare of the vertices of the LOD 0 tiles on a face
//   (steeper than kSteep, within kOnFace meters of its plane, between its ledges) at least
//   kBareRock rock (TileData::rock), and every sample of the level 0 of the horizon on a face
//   is rock.
// - The stacks of the reefs (20 to 60 m: their spires, rock_spires.hpp) stand in the LOD 3 tiles
//   and on the levels 0 to 2 of the horizon (out to 4 km), kStackSeen of their height at least
//   (the highest over them: a spire may lean off its middle) (on the level 3, out to 8 km, its samples rise to the crests of a lattice of
//   32 m, which may miss a lobed stack: how many it shows is reported); every rock of the reefs
//   kReefSeenRadius meters wide at least shows above the sea on the samples of level 0 of the
//   horizon.
#include "coast_planet_support.hpp"
#include "coast_rock_support.hpp"

#include "terrain/generation/coast.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/io/mesh_checks.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <numeric>
#include <vector>

namespace {

using coast_rock::Place;
using coast_rock::Tiles;
using terrain::RockDetail;

constexpr double kLeastFace = 40.0;
constexpr double kNotchDeep = 3.0;
constexpr double kNotchHigh = 6.0;
constexpr double kStepOff = 2.5;
constexpr double kStepOffLod3 = 8.0; // a cell of LOD 3
// A face that plunges rises on the horizon from the sea level at the shore over two pitches of its
// level (coast_faces::seenOf): its step a pitch further from the sea.
constexpr double kStepOffPlunge = 9.0;
constexpr double kLedgeAt = 20.0;
constexpr double kLedgeOff = 0.5;
constexpr double kLedgeOffLod3 = 1.5;
constexpr double kNotchFloor = 4.0;
constexpr double kNotchRoom = 2.5;
constexpr double kNotchReach = 2.0;
constexpr double kSteep = 0.5; // the up component of the normal of a vertex on a face at most
constexpr double kOnFace = 3.0;
constexpr double kBareRock = 0.9;
constexpr double kBareShare = 0.95;
constexpr double kStackSeen = 0.75; // its top, cut at a pitch of the horizon on a needle
constexpr double kReefSeenRadius = 6.0; // a disc of 8 / sqrt(2) m holds a sample of level 0
constexpr int kViewChunks = 16;         // the tiles out to 512 m
constexpr double kLevel0 = 8.0;         // the pitch of the level 0 of the horizon

constexpr std::array<Place, 4> kFaces{coast_rock::kCalanque, coast_rock::kCliff,
                                      coast_rock::kRocky, coast_rock::kFjord};

// The default world with its planet file (coast_planet_support.hpp).
const terrain::TerrainTiles& world() {
    return coast_planet::tiles();
}

void testFaces() {
    for (std::size_t i = 0; i < kFaces.size(); ++i) {
        const Place& p = kFaces[i];
        const coast_rock::Face f = coast_rock::faceAt(world().heightfield(), p);
        const terrain::RockFace& r = f.face;
        const bool notched = i == 1 || i == 2; // the cliff and the rocky coast
        std::printf("%s: a face from %.1f to %.1f m, its run %.1f m, drawn %.2f at LOD 0 and %.2f "
                    "at LOD 3; a notch %.2f m deep and %.2f m high, a cap of %.2f m\n",
                    p.name, static_cast<double>(r.foot), static_cast<double>(r.top),
                    static_cast<double>(r.run),
                    static_cast<double>(terrain::drawnWeight(r, terrain::rockDetailForLod(0))),
                    static_cast<double>(terrain::drawnWeight(r, terrain::rockDetailForLod(3))),
                    static_cast<double>(r.undercut), static_cast<double>(r.undercutHeight),
                    static_cast<double>(r.cap));
        TEST_CHECK(f.found && r.top - r.foot >= kLeastFace);
        TEST_CHECK(terrain::drawnWeight(r, terrain::rockDetailForLod(0)) >= 0.95f);
        TEST_CHECK(terrain::drawnWeight(r, terrain::rockDetailForLod(3)) >= 0.95f);
        TEST_CHECK(!notched || (r.undercut >= kNotchDeep && r.undercutHeight >= kNotchHigh));
        TEST_CHECK(i != 0 || r.undercut == 0.0f); // the calanque plunges
    }
}

// The pieces of a mesh, its vertices welded by their positions, reaching none of the sides of
// its tile (side meters wide).
std::size_t floatingPieces(const terrain::Mesh& m, float side) {
    std::map<std::array<float, 3>, std::uint32_t> ids;
    std::vector<std::uint32_t> id(m.vertices.size());
    for (std::size_t v = 0; v < m.vertices.size(); ++v) {
        const terrain::Vec3f& p = m.vertices[v];
        id[v] = ids.emplace(std::array<float, 3>{p.x, p.y, p.z}, ids.size()).first->second;
    }
    std::vector<std::uint32_t> parent(ids.size());
    std::iota(parent.begin(), parent.end(), 0U);
    const auto root = [&parent](std::uint32_t a) {
        while (parent[a] != a) {
            a = parent[a] = parent[parent[a]];
        }
        return a;
    };
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const std::uint32_t a = root(id[m.indices[i]]);
        parent[root(id[m.indices[i + 1]])] = a;
        parent[root(id[m.indices[i + 2]])] = a;
    }
    std::vector<char> reaches(ids.size(), 0);
    std::vector<char> used(ids.size(), 0);
    for (std::size_t v = 0; v < m.vertices.size(); ++v) {
        const terrain::Vec3f& p = m.vertices[v];
        const std::uint32_t r = root(id[v]);
        used[r] = 1;
        reaches[r] |= p.x == 0.0f || p.x == side || p.z == 0.0f || p.z == side ? 1 : 0;
    }
    std::size_t floating = 0;
    for (std::size_t r = 0; r < used.size(); ++r) {
        floating += used[r] != 0 && reaches[r] == 0 ? 1 : 0;
    }
    return floating;
}

void testMeshes() {
    std::size_t tiles = 0, rock = 0, triangles = 0, open = 0, multi = 0, inverted = 0;
    std::size_t floating = 0;
    std::array<std::size_t, 4> lods{};
    for (const Place& p : {coast_rock::kCalanque, coast_rock::kCliff, coast_rock::kRocky,
                           coast_rock::kFjord, coast_rock::kStack}) {
        terrain::LodParams lod;
        lod.viewDistance = kViewChunks;
        for (const terrain::TileRequest& r :
             terrain::tileRequests(terrain::selectTiles(terrain::worldToChunk(p.x, p.z), lod))) {
            const terrain::TileData t = world().generate(r);
            const auto side = static_cast<float>(terrain::tileSizeMeters(r.key.lod));
            const terrain::MeshCheck c = terrain::checkTileMesh(t.mesh, side);
            ++tiles;
            ++lods[static_cast<std::size_t>(r.key.lod)];
            rock += t.rock.empty() ? 0 : 1;
            triangles += c.triangles;
            open += c.openEdges;
            multi += c.nonManifoldEdges;
            inverted += c.inverted;
            floating += floatingPieces(t.mesh, side);
        }
    }
    std::printf("the tiles out to 512 m around the faces: %zu (LOD 0 to 3: %zu, %zu, %zu, %zu; %zu "
                "with rock), %zu triangles; %zu open edges, %zu edges of more than two triangles, "
                "%zu inverted triangles, %zu floating pieces\n",
                tiles, lods[0], lods[1], lods[2], lods[3], rock, triangles, open, multi, inverted,
                floating);
    TEST_CHECK(rock >= 100 && lods[3] >= 15);
    TEST_CHECK(open == 0 && multi == 0 && inverted == 0 && floating == 0);
}

// What a viewer sees across a face, from kLedgeAt + kSpan meters behind it to as far in front:
// the top of the LOD 0 and LOD 3 tiles and of the horizon, the water over them.
constexpr double kSpan = 18.0;
constexpr double kAcross = 0.5;

struct Profile {
    std::vector<double> lod0;
    std::vector<double> lod3;
    std::vector<double> seen;
    [[nodiscard]] static double at(std::size_t k) {
        return -(kLedgeAt + kSpan) + kAcross * static_cast<double>(k);
    }
    [[nodiscard]] static std::size_t of(double s) {
        return static_cast<std::size_t>(std::lround((s + kLedgeAt + kSpan) / kAcross));
    }
};

Profile profileOf(const coast_rock::Face& f, Tiles& tiles0, Tiles& tiles3) {
    Profile out;
    const double ax = static_cast<double>(f.face.airX);
    const double az = static_cast<double>(f.face.airZ);
    for (std::size_t k = 0; Profile::at(k) <= kLedgeAt + kSpan; ++k) {
        const double x = f.x + ax * Profile::at(k);
        const double z = f.z + az * Profile::at(k);
        const terrain::HeightSample seen = world().heightfield().seenAt(x, z, kLevel0);
        const double water = seen.water == terrain::WaterKind::None
                                 ? -1e9
                                 : static_cast<double>(seen.waterLevel);
        out.lod0.push_back(std::max(tiles0.topAt(x, z), water));
        out.lod3.push_back(std::max(tiles3.topAt(x, z), water));
        out.seen.push_back(std::max(static_cast<double>(seen.height), water));
    }
    return out;
}

// Where heights first come down through middle, from behind the face toward the air.
double stepOf(const std::vector<double>& heights, double middle) {
    for (std::size_t k = 1; k < heights.size(); ++k) {
        if (heights[k] < middle && heights[k - 1] >= middle) {
            const double t = (heights[k - 1] - middle) / (heights[k - 1] - heights[k]);
            return Profile::at(k - 1) + t * kAcross;
        }
    }
    return 1e9;
}

void testHorizon() {
    for (const Place& p : kFaces) {
        const coast_rock::Face f = coast_rock::faceAt(world().heightfield(), p);
        Tiles tiles0(world(), 0);
        Tiles tiles3(world(), 3);
        const Profile pr = profileOf(f, tiles0, tiles3);
        const std::size_t behind = Profile::of(-kLedgeAt);
        const std::size_t front = Profile::of(kLedgeAt);
        const double middle = 0.5 * (pr.lod0[behind] + pr.lod0[front]);
        const double s0 = stepOf(pr.lod0, middle);
        const double s3 = stepOf(pr.lod3, middle);
        const double seen = stepOf(pr.seen, middle);
        const std::size_t b = Profile::of(s0 - kLedgeAt);
        const std::size_t a = Profile::of(s0 + kLedgeAt);
        const double ledgeSeen =
            std::max(std::abs(pr.seen[b] - pr.lod0[b]), std::abs(pr.seen[a] - pr.lod0[a]));
        const double ledge3 =
            std::max(std::abs(pr.lod3[b] - pr.lod0[b]), std::abs(pr.lod3[a] - pr.lod0[a]));
        std::printf("%s seen across: from %.1f m behind to %.1f m in front, its step at %+.1f m "
                    "on the LOD 0 tiles, %+.1f m on the LOD 3 tiles, %+.1f m on the horizon; the "
                    "ledges %.2f m off on the horizon, %.2f m on the LOD 3 tiles\n",
                    p.name, pr.lod0[b], pr.lod0[a], s0, s3, seen, ledgeSeen, ledge3);
        const bool plunges = &p == &kFaces[0] || &p == &kFaces[3]; // the calanque, the fjord
        TEST_CHECK(std::abs(seen - s0) <= (plunges ? kStepOffPlunge : kStepOff) &&
                   std::abs(s3 - s0) <= kStepOffLod3);
        TEST_CHECK(ledgeSeen <= kLedgeOff && ledge3 <= kLedgeOffLod3);
    }
}

// Across the notch of a face, from its plane into the rock: how far a floor kNotchFloor meters
// over the sea at most lies under a ceiling kNotchRoom meters over it at least, and the lowest
// room there.
void testNotches() {
    for (const Place& p : {coast_rock::kCliff, coast_rock::kRocky}) {
        const coast_rock::Face f = coast_rock::faceAt(world().heightfield(), p);
        Tiles tiles0(world(), 0);
        double reach = 0.0;
        double floor = -1e9;
        double room = 1e9;
        for (double s = 0.0; s >= -2.0 * kNotchDeep; s -= 0.25) {
            const std::vector<coast_rock::Hit> hits =
                tiles0.hitsAt(f.x + static_cast<double>(f.face.airX) * s,
                              f.z + static_cast<double>(f.face.airZ) * s);
            if (hits.size() < 3 || !(hits[0].up > 0.0) || !(hits[1].up < 0.0) ||
                hits[0].y > kNotchFloor || hits[1].y - hits[0].y < kNotchRoom) {
                if (reach > 0.0) {
                    break;
                }
                continue;
            }
            reach = std::max(reach, -s);
            floor = std::max(floor, hits[0].y);
            room = std::min(room, hits[1].y - hits[0].y);
        }
        std::printf("%s: a notch at LOD 0 %.2f m deep from the plane of the face, its floor %.2f "
                    "m over the sea at most, %.2f m of room at least\n",
                    p.name, reach, floor, room);
        TEST_CHECK(reach >= kNotchReach && floor <= kNotchFloor && room >= kNotchRoom);
    }
}

void testBareRock() {
    std::size_t on = 0, bare = 0;
    for (const Place& p : kFaces) {
        const coast_rock::Face f = coast_rock::faceAt(world().heightfield(), p);
        const terrain::TileData t =
            world().generate({terrain::tileContaining(terrain::worldToChunk(f.x, f.z), 0), 0});
        for (std::size_t v = 0; v < t.mesh.vertices.size() && !t.rock.empty(); ++v) {
            const terrain::Vec3f& q = t.mesh.vertices[v];
            const double x = t.originX + static_cast<double>(q.x) - f.x;
            const double y = t.originY + static_cast<double>(q.y);
            const double z = t.originZ + static_cast<double>(q.z) - f.z;
            const double across = x * static_cast<double>(f.face.airX) +
                                  z * static_cast<double>(f.face.airZ);
            if (std::abs(static_cast<double>(t.mesh.normals[v].y)) > kSteep ||
                std::abs(across) > kOnFace || y < f.face.foot + 1.0f || y > f.face.top - 1.0f) {
                continue;
            }
            ++on;
            bare += static_cast<double>(t.rock[v]) >= kBareRock ? 1 : 0;
        }
    }
    std::printf("the vertices of the LOD 0 tiles on the faces: %zu, %.1f%% of them bare rock\n", on,
                100.0 * static_cast<double>(bare) /
                    static_cast<double>(std::max<std::size_t>(on, 1)));
    TEST_CHECK(on >= 1'000);
    TEST_CHECK(static_cast<double>(bare) >= kBareShare * static_cast<double>(on));
}

struct Horizon {
    terrain::Ecology ecology{terrain::kDefaultWorldSeed};
    terrain::HorizonParams params{};
    terrain::HorizonSampler sampler{world().heightfield(), ecology, params};
};

const Horizon& horizon() {
    static const Horizon h;
    return h;
}

// The samples of a level within reach meters of (x, z), with their positions.
struct Sample {
    double x;
    double z;
    terrain::HorizonCell cell;
};

std::vector<Sample> samplesNear(int level, double x, double z, double reach) {
    const Horizon& h = horizon();
    const int pitch = terrain::levelPitch(h.params, level);
    const auto n = static_cast<std::int64_t>(std::ceil(reach / pitch));
    const auto i = static_cast<std::int64_t>(std::floor(x / pitch));
    const auto k = static_cast<std::int64_t>(std::floor(z / pitch));
    const terrain::HorizonRect rect{i - n, k - n, i + n + 2, k + n + 2};
    const std::vector<terrain::HorizonCell> cells = h.sampler.sample(level, rect);
    std::vector<Sample> out;
    for (std::int64_t b = rect.z0; b < rect.z1; ++b) {
        for (std::int64_t a = rect.x0; a < rect.x1; ++a) {
            const double sx = terrain::samplePosition(a, pitch);
            const double sz = terrain::samplePosition(b, pitch);
            if (std::hypot(sx - x, sz - z) <= reach) {
                const auto at =
                    static_cast<std::size_t>((b - rect.z0) * rect.width() + (a - rect.x0));
                out.push_back({sx, sz, cells[at]});
            }
        }
    }
    return out;
}

void testHorizonRock() {
    std::size_t on = 0, rock = 0;
    for (const Place& p : kFaces) {
        const coast_rock::Face f = coast_rock::faceAt(world().heightfield(), p);
        for (const Sample& s : samplesNear(0, f.x, f.z, 40.0)) {
            const double across = (s.x - f.x) * static_cast<double>(f.face.airX) +
                                  (s.z - f.z) * static_cast<double>(f.face.airZ);
            if (std::abs(across) > kOnFace || s.cell.height < f.face.foot + 2.0f ||
                s.cell.height > f.face.top - 2.0f || s.cell.height < 0.5f) {
                continue;
            }
            ++on;
            rock += s.cell.material == terrain::SurfaceMaterial::Rock ? 1 : 0;
        }
    }
    std::printf("the samples of level 0 of the horizon on the faces: %zu, %zu of them rock\n", on,
                rock);
    TEST_CHECK(on >= 8 && rock == on);
}

// The rocks of the reefs of four stretches of coast (400 km wide), each once.
std::vector<terrain::CoastStack> reefRocks() {
    constexpr std::array<std::array<double, 2>, 4> kStretches{{{23'433'101.0, -2'128'828.0},
                                                               {-15'720'496.0, -6'266'783.0},
                                                               {-26'486'937.0, 7'219'391.0},
                                                               {23'344'528.0, -2'077'061.0}}};
    constexpr double kHalf = 200'000.0;
    std::map<std::array<double, 2>, terrain::CoastStack> rocks;
    for (const auto& [x, z] : kStretches) {
        for (const terrain::CoastStack& s :
             world().heightfield().stacksIn(x - kHalf, z - kHalf, x + kHalf, z + kHalf)) {
            rocks.emplace(std::array<double, 2>{s.x, s.z}, s);
        }
    }
    std::vector<terrain::CoastStack> out;
    for (const auto& [at, s] : rocks) {
        out.push_back(s);
    }
    return out;
}

// The highest sample of a level over a rock: within its radius, and a pitch further out on the
// levels whose samples rise to the crests of their cells (from level 1).
double highestOver(int level, const terrain::CoastStack& s) {
    const double reach = static_cast<double>(s.radius) +
                         (level > 0 ? terrain::levelPitch(horizon().params, level) : 0.0);
    double top = 0.0;
    for (const Sample& c : samplesNear(level, s.x, s.z, reach)) {
        top = std::max(top, static_cast<double>(c.cell.height));
    }
    return top;
}

// The lowest of the heights a stack stands at on the horizon (the samples of the levels 0 to 2
// over it: out to 4 km) and on the LOD 3 tiles (the highest of their top over it, every 2 m).
double lowestSeen(const terrain::CoastStack& s, Tiles& tiles3) {
    const double reach = static_cast<double>(s.radius);
    double tiles = -1e9;
    for (double dz = -reach; dz <= reach; dz += 2.0) {
        for (double dx = -reach; dx <= reach; dx += 2.0) {
            tiles = std::max(tiles, tiles3.topAt(s.x + dx, s.z + dz));
        }
    }
    double least = tiles;
    for (int level = 0; level < 3; ++level) {
        least = std::min(least, highestOver(level, s));
    }
    return least;
}

void testStacks() {
    Tiles tiles3(world(), 3);
    std::size_t stacks = 0, low = 0, reefs = 0, wide = 0, hidden = 0;
    std::size_t far = 0; // seen on the level 3 (out to 8 km)
    double lowest = 1.0;
    for (const terrain::CoastStack& s : reefRocks()) {
        if (s.height >= 20.0f) {
            ++stacks;
            const double share = lowestSeen(s, tiles3) / static_cast<double>(s.height);
            lowest = std::min(lowest, share);
            low += share >= kStackSeen ? 0 : 1;
            far += highestOver(3, s) >= kStackSeen * static_cast<double>(s.height) ? 1 : 0;
            continue;
        }
        if (s.height < 1.0f) {
            continue; // awash
        }
        const bool isWide = s.radius >= kReefSeenRadius;
        ++reefs;
        wide += isWide ? 1 : 0;
        hidden += isWide && !(highestOver(0, s) > 0.0) ? 1 : 0;
    }
    std::printf("%zu stacks of the reefs, at %.0f%% of their height at least on "
                "the horizon (levels 0 to 2, out to 4 km) and on the LOD 3 tiles, %zu of them on "
                "the level 3 (out to 8 km); %zu rocks of the reefs out of the water, %zu of them "
                "%.0f m wide or more, %zu of those hidden on the level 0 of the horizon\n",
                stacks, 100.0 * lowest, far, reefs, wide, kReefSeenRadius, hidden);
    TEST_CHECK(stacks >= 10 && low == 0);
    TEST_CHECK(wide >= 100 && hidden == 0);
}

} // namespace

int main() {
    testFaces();
    testMeshes();
    testHorizon();
    testNotches();
    testBareRock();
    testHorizonRock();
    testStacks();
    return test::result();
}
