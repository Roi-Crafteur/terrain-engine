#pragma once

// The places of the faces of the coasts in three dimensions on the default world (agent 33,
// coast_faces.hpp) and what their tests read of the tiles there: the surfaces a vertical line
// crosses in the mesh of a tile, and whether a capsule of a pawn stands clear of the rock.

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/rock_faces.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

namespace coast_rock {

// A place of a face: a point of its run (the face nearest to it is the heaviest one there).
struct Place {
    const char* name;
    double x;
    double z;
};

// The wall of the reference calanque (coast_reference_test): 50 m over the sea, its foot 26 m
// under it. A cliff and a rocky coast with a notch at their foot, 54 m and 58 m high. The wall of
// a fjord, 57 m over the water and the steep wall of the relief above it. A stack of 20 to 60 m.
inline constexpr Place kCalanque{"the wall of the calanque", 23'416'458.0, -2'119'970.0};
inline constexpr Place kCliff{"the notched cliff", 429'816.0, -855'933.0};
inline constexpr Place kRocky{"the notched rocky coast", 23'344'448.0, -2'076'633.0};
inline constexpr Place kFjord{"the wall of the fjord", 1'822'100.0, 9'925'750.0};
inline constexpr Place kStack{"the stack", 23'590'410.0, -2'323'834.0};

// The heaviest face at a place, and the middle of its run (the place moved along its air).
struct Face {
    terrain::RockFace face;
    double x = 0.0;
    double z = 0.0;
    bool found = false;
};

inline Face faceAt(const terrain::Heightfield& field, const Place& p) {
    const terrain::RockColumn r = field.rockAt(p.x, p.z);
    Face out;
    for (std::size_t i = 0; i < r.faceCount; ++i) {
        if (!out.found || r.faces[i].weight > out.face.weight) {
            out.face = r.faces[i];
            out.found = true;
        }
    }
    out.x = p.x - static_cast<double>(out.face.airX * out.face.distance);
    out.z = p.z - static_cast<double>(out.face.airZ * out.face.distance);
    return out;
}

// A surface a vertical line crosses: its height, the up component of its unit normal (its
// winding: positive on a floor, negative under a ceiling).
struct Hit {
    double y = 0.0;
    double up = 0.0;
};

// The surface of triangle i (its first index) of tile t over (x, z), appended to out if it lies
// there.
inline void hitOn(const terrain::TileData& t, std::size_t i, double x, double z,
                  std::vector<Hit>& out) {
    const terrain::Mesh& m = t.mesh;
    const double px = x - t.originX;
    const double pz = z - t.originZ;
    const terrain::Vec3f& a = m.vertices[m.indices[i]];
    const terrain::Vec3f& b = m.vertices[m.indices[i + 1]];
    const terrain::Vec3f& c = m.vertices[m.indices[i + 2]];
    const double d0 = (b.x - a.x) * (pz - a.z) - (b.z - a.z) * (px - a.x);
    const double d1 = (c.x - b.x) * (pz - b.z) - (c.z - b.z) * (px - b.x);
    const double d2 = (a.x - c.x) * (pz - c.z) - (a.z - c.z) * (px - c.x);
    const bool inside =
        (d0 >= 0.0 && d1 >= 0.0 && d2 >= 0.0) || (d0 <= 0.0 && d1 <= 0.0 && d2 <= 0.0);
    const double area = d0 + d1 + d2;
    if (!inside || !(std::abs(area) > 1e-12)) {
        return;
    }
    const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    const double nx = uy * vz - uz * vy;
    const double ny = uz * vx - ux * vz;
    const double nz = ux * vy - uy * vx;
    const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
    out.push_back({t.originY + (d1 * a.y + d2 * b.y + d0 * c.y) / area,
                   length > 0.0 ? ny / length : 0.0});
}

// The tiles of a LOD of a world, each built once, their triangles listed by the cells of the
// tile they span, and the surfaces over a point, lowest first.
class Tiles {
public:
    Tiles(const terrain::TerrainTiles& tiles, int lod) : tiles_(tiles), lod_(lod) {}

    const terrain::TileData& at(double x, double z) { return builtAt(x, z).data; }

    std::vector<Hit> hitsAt(double x, double z) {
        const Built& b = builtAt(x, z);
        std::vector<Hit> out;
        for (const std::uint32_t i : b.cells[b.cellOf(x - b.data.originX, z - b.data.originZ)]) {
            hitOn(b.data, i, x, z, out);
        }
        std::sort(out.begin(), out.end(), [](const Hit& a, const Hit& c) { return a.y < c.y; });
        return out;
    }

    // The highest surface over (x, z), or -1e9 where none lies.
    double topAt(double x, double z) {
        const std::vector<Hit> hits = hitsAt(x, z);
        return hits.empty() ? -1e9 : hits.back().y;
    }

    [[nodiscard]] const terrain::TerrainTiles& world() const { return tiles_; }

private:
    struct Built {
        terrain::TileData data;
        double cell = 1.0;
        std::size_t side = 1; // cells along a side
        std::vector<std::vector<std::uint32_t>> cells;

        [[nodiscard]] std::size_t clamped(double v) const {
            const double c = std::floor(v / cell);
            return static_cast<std::size_t>(std::clamp(c, 0.0, static_cast<double>(side - 1)));
        }
        [[nodiscard]] std::size_t cellOf(double x, double z) const {
            return clamped(z) * side + clamped(x);
        }
    };

    const Built& builtAt(double x, double z) {
        const terrain::TileKey key = terrain::tileContaining(terrain::worldToChunk(x, z), lod_);
        const std::pair<std::int32_t, std::int32_t> k{key.origin.x, key.origin.z};
        auto it = built_.find(k);
        if (it == built_.end()) {
            it = built_.emplace(k, build(key)).first;
        }
        return it->second;
    }

    Built build(const terrain::TileKey& key) const {
        Built b;
        b.data = tiles_.generate({key, 0});
        b.cell = terrain::tileCellMeters(key.lod);
        b.side = static_cast<std::size_t>(std::lround(terrain::tileSizeMeters(key.lod) / b.cell));
        b.cells.resize(b.side * b.side);
        const terrain::Mesh& m = b.data.mesh;
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const terrain::Vec3f& p = m.vertices[m.indices[i]];
            const terrain::Vec3f& q = m.vertices[m.indices[i + 1]];
            const terrain::Vec3f& r = m.vertices[m.indices[i + 2]];
            const std::size_t x0 = b.clamped(std::min({p.x, q.x, r.x}));
            const std::size_t x1 = b.clamped(std::max({p.x, q.x, r.x}));
            const std::size_t z0 = b.clamped(std::min({p.z, q.z, r.z}));
            const std::size_t z1 = b.clamped(std::max({p.z, q.z, r.z}));
            for (std::size_t cz = z0; cz <= z1; ++cz) {
                for (std::size_t cx = x0; cx <= x1; ++cx) {
                    b.cells[cz * b.side + cx].push_back(static_cast<std::uint32_t>(i));
                }
            }
        }
        return b;
    }

    const terrain::TerrainTiles& tiles_;
    int lod_;
    std::map<std::pair<std::int32_t, std::int32_t>, Built> built_;
};

// The capsule of a pawn: its radius and its height, meters (the Third Person character of
// Unreal: 42 cm, twice 96 cm).
struct Capsule {
    double radius = 0.42;
    double height = 1.92;
};

// Whether the line of a capsule at (x, z) from lo to hi meets the rock: a surface crosses it, or
// its lowest point lies in the rock (an odd number of surfaces above it).
inline bool lineInRock(Tiles& tiles, double x, double z, double lo, double hi) {
    std::size_t above = 0;
    for (const Hit& h : tiles.hitsAt(x, z)) {
        if (h.y > lo && h.y < hi) {
            return true;
        }
        above += h.y > lo ? 1 : 0;
    }
    return above % 2 == 1;
}

// Whether capsule c, its lowest point at bottom over (x, z), stands clear of the rock: its axis,
// and its lines at half its radius and at its radius in 8 directions, each from the sphere of
// its ends.
inline bool clearOfRock(Tiles& tiles, double x, double z, double bottom, const Capsule& c) {
    for (int ring = 0; ring < 3; ++ring) {
        const double rho = 0.5 * c.radius * ring;
        const double lift = c.radius - std::sqrt(std::max(0.0, c.radius * c.radius - rho * rho));
        for (int k = 0; k < (ring == 0 ? 1 : 8); ++k) {
            const double a = k * std::numbers::pi / 4.0;
            if (lineInRock(tiles, x + rho * std::cos(a), z + rho * std::sin(a), bottom + lift,
                           bottom + c.height - lift)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace coast_rock
