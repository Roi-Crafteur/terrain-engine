// Can the character walk into a tunnel the player digs block by block, and out again (agent 41)?
// Under the ground, a room of 4 x 3 x 5 blocks with a flat floor, and from its east wall a tunnel
// of 2 x 2, 1 x 3 or 1 x 2 blocks dug 6 blocks long, closed at its end. A capsule (radius, half
// height) walks from the middle of the room to the end of the tunnel and back, every 5 cm, on the
// floor of the LOD 0 mesh:
// - its lower sphere goes down from the middle of the tunnel until it touches the mesh, its upper
//   sphere up: the capsule fits when the room between them holds it, nowhere closer to a triangle
//   than its radius;
// - the floor holds it as the CharacterMovement of Unreal decides (ComputeFloorDist): the contact
//   of its lower sphere when its normal is within the walkable angle (44.765 degrees, the default
//   the game keeps), else the surface a vertical line from its center meets below (where the
//   flat floor of the room meets the V of a tunnel the sphere touches a facet of 53 degrees, the
//   line the floor under it);
// - from one place to the next it never climbs more than the step of the CharacterMovement
//   (45 cm).
// A capsule turns about its vertical axis without changing shape: standing at the end of the
// tunnel is turning there. The capsule of the game (SandboxCharacter_CMC of GASP: 30 cm, half
// height 86 cm, read in Unreal by the orchestrator) must walk in and out of the three tunnels; the
// capsules measured before it (34/88, 35/90, 42/96) must fit and walk in the 2 x 2 one and fit in
// the 1 x 3 one, and are printed for the 1 x 2 one.
//
// In a tunnel one block wide, Marching Cubes joins the foot of each wall (at the height of the
// center of the lowest block) to the middle of the floor half a block lower: a floor in V. The side
// walls of the removed blocks stand at kSideWall (0.531 m) from their centers, so the V has
// atan(0.5 / 0.531) = 43.3 degrees, walkable (45.0 at 0.5 m).
#include "test_assert.hpp"

#include "terrain/storage/modifications.hpp"
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using terrain::BlockEdit;
using terrain::Vec3f;

constexpr double kWalkableDegrees = 44.765;
constexpr double kStepMeters = 0.45;
constexpr double kPathStep = 0.05;   // meters between two places of the walk
constexpr double kContact = 0.0005;  // meters: a sphere touches the mesh
constexpr double kDegrees = 57.29577951308232;

struct Capsule {
    const char* name = "";
    double radius = 0.0;     // meters
    double halfHeight = 0.0; // from the center to the top of the cap, meters
};

struct Triangle {
    std::array<Vec3f, 3> v;
    std::array<float, 2> low{};  // its box in x and z
    std::array<float, 2> high{};
};

Vec3f sub(Vec3f a, Vec3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f add(Vec3f a, Vec3f b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3f scale(Vec3f a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

float dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// The point of a triangle nearest to p (Ericson, Real-Time Collision Detection, 5.1.5).
Vec3f closest(const Triangle& t, Vec3f p) {
    const Vec3f a = t.v[0], b = t.v[1], c = t.v[2];
    const Vec3f ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) {
        return a;
    }
    const Vec3f bp = sub(p, b);
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        return add(a, scale(ab, d1 / (d1 - d3)));
    }
    const Vec3f cp = sub(p, c);
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        return add(a, scale(ac, d2 / (d2 - d6)));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        return add(b, scale(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    }
    const float denom = 1.0f / (va + vb + vc);
    return add(a, add(scale(ab, vb * denom), scale(ac, vc * denom)));
}

struct Near {
    Vec3f at{};
    double distance = 1e9;
};

Near nearest(const std::vector<Triangle>& triangles, Vec3f p) {
    Near n;
    for (const Triangle& t : triangles) {
        const Vec3f q = closest(t, p);
        const Vec3f d = sub(p, q);
        const double distance = std::sqrt(static_cast<double>(dot(d, d)));
        if (distance < n.distance) {
            n = {q, distance};
        }
    }
    return n;
}

// The triangles of a mesh whose box comes within reach of the vertical line (x, z).
std::vector<Triangle> around(const std::vector<Triangle>& mesh, double x, double z, double reach) {
    std::vector<Triangle> out;
    for (const Triangle& t : mesh) {
        if (t.low[0] <= x + reach && t.high[0] >= x - reach && t.low[1] <= z + reach &&
            t.high[1] >= z - reach) {
            out.push_back(t);
        }
    }
    return out;
}

struct Stand {
    bool fits = false;
    double low = 0.0;   // the center of the lower sphere, meters
    double room = 0.0;  // the height between the centers of the two spheres, meters
    double floor = 0.0; // the angle of the contact under the capsule, degrees
    double under = 90.0; // the angle of the triangle a vertical line meets below its center
};

// The angle from the vertical of the normal of the first triangle a vertical line from p meets
// below it, 90 when it meets none.
double underAngle(const std::vector<Triangle>& mesh, Vec3f p) {
    double best = -1e9;
    double angle = 90.0;
    for (const Triangle& t : mesh) {
        const Vec3f a = t.v[0], b = t.v[1], c = t.v[2];
        const double d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::abs(d) < 1e-12) {
            continue; // a wall
        }
        const double l1 = ((b.z - c.z) * (p.x - c.x) + (c.x - b.x) * (p.z - c.z)) / d;
        const double l2 = ((c.z - a.z) * (p.x - c.x) + (a.x - c.x) * (p.z - c.z)) / d;
        const double l3 = 1.0 - l1 - l2;
        if (l1 < -1e-6 || l2 < -1e-6 || l3 < -1e-6) {
            continue;
        }
        const double y = l1 * a.y + l2 * b.y + l3 * c.y;
        if (y > p.y || y < best) {
            continue;
        }
        const Vec3f e1 = sub(b, a);
        const Vec3f e2 = sub(c, a);
        const double nx = e1.y * e2.z - e1.z * e2.y;
        const double ny = e1.z * e2.x - e1.x * e2.z;
        const double nz = e1.x * e2.y - e1.y * e2.x;
        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        best = y;
        angle = std::acos(std::clamp(std::abs(ny) / length, 0.0, 1.0)) * kDegrees;
    }
    return angle;
}

// From y, the center of a sphere of the capsule moves along the vertical line by dir (+1 or -1)
// until it touches the mesh: each move is the distance to the mesh less the radius, so that it
// never goes through a triangle.
double touch(const std::vector<Triangle>& mesh, double x, double y, double z, double dir,
             double radius) {
    for (int i = 0; i < 200; ++i) {
        const Vec3f p{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        const double gap = nearest(mesh, p).distance - radius;
        if (gap < kContact) {
            break;
        }
        y += dir * gap;
    }
    return y;
}

// The capsule on the vertical line (x, z), from the height middle.
Stand standAt(const std::vector<Triangle>& all, double x, double z, double middle,
              const Capsule& capsule) {
    const std::vector<Triangle> mesh = around(all, x, z, capsule.radius + 0.6);
    const auto point = [&](double y) {
        return Vec3f{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    };
    Stand stand;
    if (nearest(mesh, point(middle)).distance < capsule.radius) {
        return stand; // too narrow
    }
    stand.low = touch(mesh, x, middle, z, -1.0, capsule.radius);
    const double high = touch(mesh, x, middle, z, 1.0, capsule.radius);
    stand.room = high - stand.low;
    const double needed = 2.0 * (capsule.halfHeight - capsule.radius);
    // The cylinder between the spheres, every 2 cm.
    bool clear = stand.room >= needed;
    for (double y = stand.low; clear && y <= stand.low + needed; y += 0.02) {
        clear = nearest(mesh, point(y)).distance >= capsule.radius - 1e-3;
    }
    stand.fits = clear;
    const Near contact = nearest(mesh, point(stand.low));
    const Vec3f up = sub(point(stand.low), contact.at);
    stand.floor = std::acos(std::clamp(static_cast<double>(up.y) / contact.distance, -1.0, 1.0)) *
                  kDegrees;
    stand.under = underAngle(mesh, point(stand.low));
    return stand;
}

struct Tunnel {
    int width = 0;  // blocks along z
    int height = 0; // blocks along y
};

// The room and the tunnel: blocks x in [kRoomX0, kRoomX1] then [kTunnelX0, kTunnelX1] east of it.
constexpr int kRoomX0 = 1;
constexpr int kRoomX1 = 4;
constexpr int kTunnelX0 = 5;
constexpr int kTunnelX1 = 10; // 6 blocks
constexpr int kRoomZ0 = 5;
constexpr int kRoomZ1 = 9;
constexpr int kTunnelZ0 = 7;

struct Dug {
    std::vector<Triangle> mesh; // world meters
    int bottom = 0;             // the lowest row of blocks
};

// The triangles of the LOD 0 tile of the chunk (0, 0) around the dug blocks, in world meters.
std::vector<Triangle> meshAround(const terrain::TerrainTiles& tiles) {
    std::vector<Triangle> out;
    const terrain::TileData data = tiles.generate({{{0, 0}, 0}, 0});
    const terrain::Mesh& mesh = data.mesh;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        Triangle tri;
        for (std::size_t e = 0; e < 3; ++e) {
            const Vec3f& v = mesh.vertices[mesh.indices[t + e]];
            tri.v[e] = {static_cast<float>(data.originX + v.x),
                        static_cast<float>(data.originY + v.y),
                        static_cast<float>(data.originZ + v.z)};
        }
        tri.low = {std::min({tri.v[0].x, tri.v[1].x, tri.v[2].x}),
                   std::min({tri.v[0].z, tri.v[1].z, tri.v[2].z})};
        tri.high = {std::max({tri.v[0].x, tri.v[1].x, tri.v[2].x}),
                    std::max({tri.v[0].z, tri.v[1].z, tri.v[2].z})};
        if (tri.high[0] > kRoomX0 - 2.0f && tri.low[0] < kTunnelX1 + 2.0f &&
            tri.high[1] > kRoomZ0 - 2.0f && tri.low[1] < kRoomZ1 + 2.0f) {
            out.push_back(tri);
        }
    }
    return out;
}

Dug dig(const Tunnel& tunnel) {
    terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    Dug dug;
    dug.bottom = static_cast<int>(std::floor(tiles.heightfield().heightAt(7.0, 8.0))) - 7;
    for (int x = kRoomX0; x <= kRoomX1; ++x) {
        for (int z = kRoomZ0; z <= kRoomZ1; ++z) {
            for (int y = dug.bottom; y < dug.bottom + 3; ++y) {
                tiles.setBlock({x, y, z}, BlockEdit::Removed);
            }
        }
    }
    for (int x = kTunnelX0; x <= kTunnelX1; ++x) {
        for (int z = kTunnelZ0; z < kTunnelZ0 + tunnel.width; ++z) {
            for (int y = dug.bottom; y < dug.bottom + tunnel.height; ++y) {
                tiles.setBlock({x, y, z}, BlockEdit::Removed);
            }
        }
    }
    dug.mesh = meshAround(tiles);
    return dug;
}

// Scene b of the checks in Unreal (terrain_unreal/BREAK_PLACE.md), on the ground of the world
// here: a sphere of 5 m broken on the ground (Terrain.EditAim dig sphere 2.4375 at a point of
// the ground: centered on the block of that point), then from its east wall a tunnel of 1 x 2
// blocks dug block by block at the two lowest rows of the sphere, its 6 blocks east of the
// center from 2 to 7 m (the block at 2 m of the upper row is the sphere's). On the plain of the
// scene the ground stands 0.19 m over the center of its block: the tunnel keeps a roof of
// ground 0.7 m thick. Here the sphere is centered on the block under the ground (floor), for the
// same roof whatever the height of the ground.
constexpr int kCraterX = 3;
constexpr int kCraterZ = 8;

Dug digCrater() {
    terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const int center =
        static_cast<int>(std::floor(tiles.heightfield().heightAt(kCraterX, kCraterZ)));
    terrain::VolumeEdit sphere;
    sphere.mode = terrain::EditMode::Remove;
    sphere.shape = terrain::EditShape::Sphere;
    sphere.anchor = {kCraterX, center, kCraterZ};
    sphere.radius = 39; // sixteenths of a meter: the sphere of 5 m of the player
    (void)tiles.applyEdit(sphere);
    for (int x = kCraterX + 2; x <= kCraterX + 7; ++x) {
        tiles.setBlock({x, center - 2, kCraterZ}, BlockEdit::Removed);
        tiles.setBlock({x, center - 1, kCraterZ}, BlockEdit::Removed);
    }
    Dug dug;
    dug.bottom = center - 2;
    dug.mesh = meshAround(tiles);
    return dug;
}

struct Walk {
    bool fits = true;         // at every place, in and out
    double margin = 1e9;      // the least room less what the capsule needs, meters
    double contact = 0.0;     // the steepest contact under the capsule, degrees
    double floor = 0.0;       // the steepest floor the CharacterMovement takes, degrees
    double climb = 0.0;       // the largest rise from one place to the next, either way, meters
    double tunnelFloor = 0.0; // the steepest floor inside the tunnel, degrees
};

// Along the line z from x = from to x = to, every kPathStep: into the last block of a tunnel, the
// front of the capsule 45 cm from its end wall, at the foot of the slope Marching Cubes draws
// there (the sphere would stand on it a few cm further). The walk back goes through the same
// places, its climbs being the descents of the walk in. The tunnel begins at tunnelFrom.
Walk walkAlong(const Dug& dug, double z, double middle, double from, double to, double tunnelFrom,
               const Capsule& capsule) {
    const double needed = 2.0 * (capsule.halfHeight - capsule.radius);
    Walk w;
    double previous = 0.0;
    bool first = true;
    for (double x = from; x <= to + 1e-9; x += kPathStep) {
        const Stand s = standAt(dug.mesh, x, z, middle, capsule);
        w.fits = w.fits && s.fits;
        w.margin = std::min(w.margin, s.room - needed);
        w.contact = std::max(w.contact, s.floor);
        // The contact when walkable, else the line under the center (ComputeFloorDist).
        const double floor = s.floor <= kWalkableDegrees ? s.floor : s.under;
        w.floor = std::max(w.floor, floor);
        if (x >= tunnelFrom) {
            w.tunnelFloor = std::max(w.tunnelFloor, floor);
        }
        if (!first) {
            w.climb = std::max(w.climb, std::abs(s.low - previous));
        }
        previous = s.low;
        first = false;
    }
    return w;
}

// From the middle of the room to the end of the tunnel.
Walk walk(const Dug& dug, const Tunnel& tunnel, const Capsule& capsule) {
    return walkAlong(dug, kTunnelZ0 + (tunnel.width - 1) / 2.0,
                     dug.bottom + (tunnel.height - 1) / 2.0, (kRoomX0 + kRoomX1) / 2.0,
                     kTunnelX1 - 0.25, kTunnelX0, capsule);
}

void printWalk(const Capsule& capsule, const Walk& w) {
    std::printf("  capsule %s, %.0f / %.0f cm: %s (margin %+.1f cm), floor at most "
                "%.1f degrees (%.1f in the tunnel, %s; contacts up to %.1f), climbs "
                "%.0f cm at most%s\n",
                capsule.name, capsule.radius * 100.0, capsule.halfHeight * 100.0,
                w.fits ? "walks in and out" : "does not fit", w.margin * 100.0, w.floor,
                w.tunnelFloor, w.floor <= kWalkableDegrees ? "walkable" : "too steep",
                w.contact, w.climb * 100.0, w.climb <= kStepMeters ? "" : " (too high a step)");
}

bool passes(const Walk& w) {
    return w.fits && w.floor <= kWalkableDegrees && w.climb <= kStepMeters;
}

// How far the walls of a tunnel stand from its axis at the middle of its height, halfway along:
// from the axis toward +z, each move the distance to the mesh, until it touches.
double wallAt(const Dug& dug, const Tunnel& tunnel) {
    const double axis = kTunnelZ0 + (tunnel.width - 1) / 2.0;
    const double y = dug.bottom + (tunnel.height - 1) / 2.0;
    const double x = (kTunnelX0 + kTunnelX1) / 2.0;
    const std::vector<Triangle> mesh = around(dug.mesh, x, axis, 2.0);
    double z = axis;
    for (int i = 0; i < 200; ++i) {
        const Vec3f p{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        const double gap = nearest(mesh, p).distance;
        if (gap < 1e-4) {
            break;
        }
        z += gap;
    }
    return z - axis;
}

} // namespace

int main() {
    const Capsule game{"game (GASP)", 0.30, 0.86};
    const std::array<Capsule, 4> capsules = {game, Capsule{"34/88", 0.34, 0.88},
                                             Capsule{"35/90", 0.35, 0.90},
                                             Capsule{"42/96", 0.42, 0.96}};
    const std::array<Tunnel, 3> tunnels = {Tunnel{2, 2}, Tunnel{1, 3}, Tunnel{1, 2}};
    for (const Tunnel& tunnel : tunnels) {
        const Dug dug = dig(tunnel);
        std::printf("tunnel %d x %d, 6 blocks from a room: its walls %.3f m from its axis\n",
                    tunnel.width, tunnel.height, wallAt(dug, tunnel));
        for (const Capsule& capsule : capsules) {
            const Walk w = walk(dug, tunnel, capsule);
            printWalk(capsule, w);
            if (&capsule == &capsules[0] || tunnel.width == 2) {
                TEST_CHECK(passes(w));
            } else if (tunnel.height == 3) {
                TEST_CHECK(w.fits);
            }
        }
    }
    // Scene b of the checks in Unreal: from the bottom of the sphere to the end of the tunnel.
    const Dug scene = digCrater();
    const Walk w = walkAlong(scene, kCraterZ, scene.bottom + 0.5, kCraterX, kCraterX + 7 - 0.25,
                             kCraterX + 2, game);
    std::printf("scene b: from the bottom of a sphere of 5 m into a tunnel of 1 x 2 blocks, 6 "
                "long:\n");
    printWalk(game, w);
    TEST_CHECK(passes(w));
    return test::result();
}
