// Can the character walk in a tunnel the player digs block by block (agent 41)? Tunnels of 2 x 2,
// 1 x 3 and 1 x 2 blocks dug 6 m under the ground; a capsule (radius, half height) stands at
// several places along each, on the floor of its LOD 0 mesh: from the middle of the tunnel its
// lower sphere goes down until it touches the mesh, its upper sphere up; the capsule fits when the
// room between them holds it, nowhere closer to a triangle than its radius. The floor holds it
// when the normal of the contact under it is within the walkable angle of the CharacterMovement
// of Unreal (44.765 degrees by default).
//
// The 2 x 2 tunnel must take the three capsules: its floor and ceiling are flat. In a tunnel one
// block wide, Marching Cubes joins the foot of each wall (at the height of the center of the
// lowest block) to the middle of the floor half a block lower: a floor in V and a ceiling in A at
// 45 degrees, 2 m high in the middle and 1 m against the walls. The 1 x 3 and 1 x 2 tunnels are
// measured and printed (what is missing, the angle of the floor), for a decision of the
// orchestrator: flattening the V raises the floor and takes room, deepening it makes it too steep.
#include "test_assert.hpp"

#include "terrain/storage/modifications.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using terrain::BlockEdit;
using terrain::Vec3f;

constexpr double kWalkableDegrees = 44.765;
constexpr double kStep = 0.002; // meters, the steps of the spheres

struct Capsule {
    double radius = 0.0;     // meters
    double halfHeight = 0.0; // from the center to the top of the cap, meters
};

struct Triangle {
    std::array<Vec3f, 3> v;
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

// The nearest point of the triangles to p, and its distance.
std::pair<Vec3f, double> nearest(const std::vector<Triangle>& triangles, Vec3f p) {
    double best = 1e9;
    Vec3f at{};
    for (const Triangle& t : triangles) {
        const Vec3f q = closest(t, p);
        const Vec3f d = sub(p, q);
        const double distance = std::sqrt(static_cast<double>(dot(d, d)));
        if (distance < best) {
            best = distance;
            at = q;
        }
    }
    return {at, best};
}

struct Stand {
    bool fits = false;
    double room = 0.0;  // the height between the centers of the two spheres, meters
    double floor = 0.0; // the angle of the contact under the capsule, degrees
};

// The capsule on the vertical line (x, z) of the tunnel, from the height middle.
Stand standAt(const std::vector<Triangle>& mesh, double x, double z, double middle,
              const Capsule& capsule) {
    const auto point = [&](double y) {
        return Vec3f{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    };
    Stand stand;
    if (nearest(mesh, point(middle)).second < capsule.radius) {
        return stand; // too narrow
    }
    double low = middle;
    while (nearest(mesh, point(low - kStep)).second >= capsule.radius && low > middle - 3.0) {
        low -= kStep;
    }
    double high = middle;
    while (nearest(mesh, point(high + kStep)).second >= capsule.radius && high < middle + 3.0) {
        high += kStep;
    }
    stand.room = high - low;
    const double needed = 2.0 * (capsule.halfHeight - capsule.radius);
    // The cylinder between the spheres, every 2 cm.
    bool clear = stand.room >= needed;
    for (double y = low; clear && y <= low + needed; y += 0.02) {
        clear = nearest(mesh, point(y)).second >= capsule.radius - 1e-3;
    }
    stand.fits = clear;
    const auto [contact, distance] = nearest(mesh, point(low - kStep));
    const Vec3f up = sub(point(low - kStep), contact);
    stand.floor = std::acos(std::clamp(static_cast<double>(up.y) / distance, -1.0, 1.0)) *
                  57.29577951308232;
    return stand;
}

struct Tunnel {
    int width = 0;  // blocks along x
    int height = 0; // blocks along y
};

struct Walk {
    bool fits = true;       // at every place along the tunnel
    double shortfall = 0.0; // the most missing at one place, meters
    double floor = 0.0;     // the steepest floor under the capsule, degrees
};

// The triangles of the LOD 0 tile of the chunk (0, 0) near the tunnel, in world meters.
std::vector<Triangle> tunnelMesh(const terrain::TileData& data, double x0, double x1) {
    std::vector<Triangle> out;
    const terrain::Mesh& mesh = data.mesh;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        Triangle tri;
        bool near = false;
        for (std::size_t e = 0; e < 3; ++e) {
            const Vec3f& v = mesh.vertices[mesh.indices[t + e]];
            tri.v[e] = {static_cast<float>(data.originX + v.x),
                        static_cast<float>(data.originY + v.y),
                        static_cast<float>(data.originZ + v.z)};
            near = near || (tri.v[e].x > x0 - 2.0 && tri.v[e].x < x1 + 2.0);
        }
        if (near) {
            out.push_back(tri);
        }
    }
    return out;
}

Walk walk(const Tunnel& tunnel, const Capsule& capsule) {
    terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const int bottom = static_cast<int>(std::floor(tiles.heightfield().heightAt(7.0, 8.0))) - 6;
    const int x0 = 6;
    for (int z = 3; z <= 13; ++z) {
        for (int y = bottom; y < bottom + tunnel.height; ++y) {
            for (int x = x0; x < x0 + tunnel.width; ++x) {
                tiles.setBlock({x, y, z}, BlockEdit::Removed);
            }
        }
    }
    const terrain::TileData data = tiles.generate({{{0, 0}, 0}, 0});
    const std::vector<Triangle> mesh = tunnelMesh(data, x0, x0 + tunnel.width - 1);
    const double middle = bottom + (tunnel.height - 1) / 2.0;
    const double center = x0 + (tunnel.width - 1) / 2.0;
    const double needed = 2.0 * (capsule.halfHeight - capsule.radius);
    Walk result;
    for (const double z : {6.0, 7.3, 8.0, 9.6, 10.0}) {
        std::optional<Stand> best;
        for (double dx = -0.25; dx <= 0.2501; dx += 0.05) {
            const Stand s = standAt(mesh, center + dx, z, middle, capsule);
            if (!best || (s.fits && !best->fits) || (s.fits == best->fits && s.room > best->room)) {
                best = s;
            }
        }
        result.fits = result.fits && best->fits;
        result.shortfall = std::max(result.shortfall, needed - best->room);
        result.floor = std::max(result.floor, best->floor);
    }
    return result;
}

} // namespace

int main() {
    const std::array<Capsule, 3> capsules = {Capsule{0.34, 0.88}, Capsule{0.35, 0.90},
                                             Capsule{0.42, 0.96}};
    const std::array<Tunnel, 3> tunnels = {Tunnel{2, 2}, Tunnel{1, 3}, Tunnel{1, 2}};
    for (const Tunnel& tunnel : tunnels) {
        for (const Capsule& capsule : capsules) {
            const Walk w = walk(tunnel, capsule);
            const bool walkable = w.floor <= kWalkableDegrees;
            std::printf("tunnel %d x %d, capsule %.0f / %.0f cm: %s, floor at %.1f degrees (%s)\n",
                        tunnel.width, tunnel.height, capsule.radius * 100.0,
                        capsule.halfHeight * 100.0,
                        w.fits ? "fits" : "does not fit", w.floor,
                        walkable ? "walkable" : "too steep");
            if (!w.fits) {
                std::printf("    %.1f cm missing\n", w.shortfall * 100.0);
            }
            if (tunnel.width == 2) {
                TEST_CHECK(w.fits && walkable);
            }
            if (tunnel.height == 3) {
                TEST_CHECK(w.fits); // high enough; its floor is printed
            }
        }
    }
    return test::result();
}
