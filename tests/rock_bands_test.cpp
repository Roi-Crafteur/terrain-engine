// No vertical bands on the walls of the slots of the falls (phase 12A): along a wall, at
// mid-height, the normals of the vertices of the tiles of LOD 0 turn by kBandTurn at most from one
// vertex to the next (kBandStep meters apart at most). A face standing within a cell of where its
// wall stops (slot_wall.hpp: the valleys hand it on half a meter behind the rim) left ridges every
// few meters whose normals turned by about 40 degrees (9de109e): the faces of the slots are not
// drawn until the valleys hand their wall on far enough (slot_faces.hpp, kSlotFacesDrawn). The
// walls are found from the valleys (RiverCarve::wall, 6 m across and 10 m high at least), below
// the feet of the falls of the small world, whatever the faces drawn. (The walls of the gorges are
// rough by design, their strata, couloirs and grain: their normals turn more than that with or
// without their faces, which change their images by no pixel at the walls checked.)
#include "falls_support.hpp"

#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr double kBandTurn = 25.0; // degrees
constexpr double kBandStep = 1.5;  // meters along the wall
constexpr double kHalfLength = 16.0; // of the stretch of wall checked, meters
constexpr double kBand = 0.5;        // the vertices within this of mid-height
constexpr double kNear = 3.0;        // and of the line of the wall
constexpr double kLeastWidth = 6.0;
constexpr double kLeastHeight = 10.0;
constexpr std::size_t kWalls = 4;    // of the slots
constexpr double kAhead = 6.0;       // the sections below the feet of the falls

// A wall: a point of its face, the unit direction of its air, the height checked.
struct Wall {
    double x = 0.0;
    double z = 0.0;
    double airX = 1.0;
    double airZ = 0.0;
    double y = 0.0;
};

// The LOD 0 tiles of the small world, each built once.
class Tiles {
public:
    explicit Tiles(const terrain::TerrainTiles& tiles) : tiles_(tiles) {}
    const terrain::TileData& at(double x, double z) {
        const terrain::ChunkCoord chunk{
            static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
            static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
        const terrain::TileKey key = terrain::tileContaining(chunk, 0);
        const std::pair<std::int32_t, std::int32_t> k{key.origin.x, key.origin.z};
        auto it = built_.find(k);
        if (it == built_.end()) {
            it = built_.emplace(k, tiles_.generate({key, 0})).first;
        }
        return it->second;
    }

private:
    const terrain::TerrainTiles& tiles_;
    std::map<std::pair<std::int32_t, std::int32_t>, terrain::TileData> built_;
};

// The largest turn of the normals from one vertex to the next along wall w (degrees), and how many
// vertices were compared.
std::pair<double, std::size_t> turnAlong(Tiles& tiles, const Wall& w) {
    const double ux = -w.airZ; // along the wall
    const double uz = w.airX;
    std::vector<std::array<double, 3>> found; // along, normal x, normal z
    for (double a = -kHalfLength; a <= kHalfLength; a += 4.0) {
        for (const double b : {-kNear, kNear}) {
            static_cast<void>(tiles.at(w.x + ux * a + w.airX * b, w.z + uz * a + w.airZ * b));
        }
    }
    std::map<const terrain::TileData*, bool> seen;
    for (double a = -kHalfLength; a <= kHalfLength; a += 4.0) {
        for (const double b : {-kNear, kNear}) {
            const terrain::TileData& t = tiles.at(w.x + ux * a + w.airX * b, w.z + uz * a + w.airZ * b);
            if (seen.emplace(&t, true).second) {
                const terrain::Mesh& m = t.mesh;
                for (std::size_t i = 0; i < m.vertices.size(); ++i) {
                    const terrain::Vec3f& p = m.vertices[i];
                    const double px = t.originX + p.x - w.x;
                    const double pz = t.originZ + p.z - w.z;
                    const double along = px * ux + pz * uz;
                    const double across = px * w.airX + pz * w.airZ;
                    const terrain::Vec3f& n = m.normals[i];
                    const double horizontal = std::hypot(n.x, n.z);
                    if (std::abs(t.originY + p.y - w.y) <= kBand && std::abs(across) <= kNear &&
                        std::abs(along) <= kHalfLength && horizontal > 0.5) {
                        found.push_back({along, n.x / horizontal, n.z / horizontal});
                    }
                }
            }
        }
    }
    std::sort(found.begin(), found.end());
    double most = 0.0;
    std::size_t compared = 0;
    for (std::size_t i = 1; i < found.size(); ++i) {
        if (found[i][0] - found[i - 1][0] > kBandStep || found[i][0] == found[i - 1][0]) {
            continue;
        }
        const double c = std::clamp(found[i][1] * found[i - 1][1] + found[i][2] * found[i - 1][2],
                                    -1.0, 1.0);
        most = std::max(most, std::acos(c) * 180.0 / 3.14159265358979323846);
        ++compared;
    }
    return {most, compared};
}

// The walls of the slots across the sections below the feet of the falls: from the valleys
// (RiverCarve::wall), their middle and their air.
std::vector<Wall> slotWalls(const falls::World& w, falls::Reaches& reaches) {
    std::vector<Wall> out;
    for (const std::size_t r : falls::steepReaches(w, reaches)) {
        for (const terrain::RiverFall& f : reaches[r].falls) {
            double dx = f.footX - f.x;
            double dz = f.footZ - f.z;
            const double length = std::hypot(dx, dz);
            if (!(length > 0.0) || out.size() >= kWalls) {
                continue;
            }
            dx /= length;
            dz /= length;
            for (double t = -30.0; t <= 30.0 && out.size() < kWalls; t += 1.0) {
                const double x = f.footX + dx * kAhead - dz * t;
                const double z = f.footZ + dz * kAhead + dx * t;
                const terrain::RiverCarve c = w.hydrology->riversAt(x, z, w.field.heightAt(x, z));
                const terrain::SlotWall& s = c.wall;
                if (s.width >= kLeastWidth && s.top - s.foot >= kLeastHeight &&
                    std::abs(s.distance) < 0.5f) {
                    out.push_back({x - s.airX * s.distance, z - s.airZ * s.distance, s.airX,
                                   s.airZ, 0.5 * (s.foot + s.top)});
                    break; // one wall a fall
                }
            }
        }
    }
    return out;
}

void warmCourses(const falls::World& w) {
    std::vector<std::size_t> candidates;
    for (const auto& segment : w.planet->rivers().segments) {
        const auto& v = segment.vertices;
        for (std::size_t k = 0; k + 1 < v.size(); ++k) {
            if (v[k].elevation - v[k + 1].elevation >= falls::kSteepGraph) {
                for (const std::size_t r : w.hydrology->reachesNear(v[k].x, v[k].z, 1.0)) {
                    candidates.push_back(r);
                }
            }
        }
    }
    std::atomic<std::size_t> next{0};
    std::vector<std::jthread> pool;
    const unsigned threads = std::max(1U, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (std::size_t i = next++; i < candidates.size(); i = next++) {
                static_cast<void>(w.hydrology->reach(candidates[i]));
            }
        });
    }
}

} // namespace

int main() {
    const falls::World w = falls::makeWorld();
    warmCourses(w);
    falls::Reaches reaches(*w.hydrology);
    const terrain::TerrainTiles field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt,
                                      golden::kSmallPlanet, w.hydrology);
    Tiles tiles(field);
    const std::vector<Wall> walls = slotWalls(w, reaches);
    double most = 0.0;
    std::size_t compared = 0;
    for (const Wall& wall : walls) {
        const auto [turn, count] = turnAlong(tiles, wall);
        most = std::max(most, turn);
        compared += count;
    }
    std::printf("the walls of the slots: %zu, %zu pairs of vertices along them at mid-height, the "
                "normals turning by %.1f degrees at most from one to the next\n",
                walls.size(), compared, most);
    TEST_CHECK(walls.size() == kWalls && compared >= 40 && most <= kBandTurn);
    return test::result();
}
