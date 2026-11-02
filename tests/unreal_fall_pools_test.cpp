// The pools below the falls of the small world as Unreal draws them (phase 12A), in the LOD 0
// tiles around the foot of every fall of the reaches of the falls:
//   - Still pools: in the pool of a fall (kPastFoot meters past where its water meets it and
//     within TileFall::pool, kPoolLeast meters when unknown), in its channel, no vertex of the
//     surface stands a little above the water below the fall (kFlat to kAbove meters): the core
//     gives a column beside its foot the level of its face just above the foot, a bump whose
//     slopes light the foam of the rapids. (Between the lip and the foot, under the curtain, the
//     water runs down the face.)
//   - The foam of a fall along the foot of its curtain (fall_foam.hpp): as wide as its water
//     falling at least or narrowed by a lip at a side, never longer downstream than wide; its
//     vertices drawn as its shape (uv0, uv2.v).
//   - Foam clear of the next lip: its ragged rim (kFoamRim of its semi-axes, the material) stays
//     kClearLeast meters short of the edge of the lip of each fall its pool runs to (whose water
//     above stands at the water below it): never a veil hanging over the edge, the view through
//     it going down the fall. Where a tile the foam may reach does not know that fall (its lip
//     more than kFallNear meters from the tile along x or z), the foam keeps its size in every
//     tile: counted, not checked.
//   - One foam: the same shape in every tile that draws it.
#include "fall_curtains.hpp"
#include "fall_edges.hpp"
#include "fall_foam.hpp"
#include "falls_support.hpp"
#include "tile_water_mesh.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace {

using terrain::TileData;
using terrain::TileFall;
using terrain::unreal::UnrealWaterMesh;

constexpr double kPoolLeast = 4.0;
// The diagonal of a cell of LOD 0 and a little more: a cell across the foot holds the face.
constexpr double kPastFoot = 1.5;
constexpr double kFlat = 0.01;
constexpr double kAbove = 0.6;
constexpr double kSideMargin = 2.5;
constexpr double kClearLeast = 2.0;
constexpr int kRimPoints = 256;
constexpr int kAround = 2; // tiles of LOD 0 on each side of the tile of the foot of a fall

struct Checks {
    std::size_t tiles = 0;
    std::size_t vertices = 0;   // of the surface in the pools of the falls
    std::size_t bumps = 0;      // of them a little above the water below their fall
    double highest = 0.0;       // meters above it at most
    std::size_t shapes = 0;     // foams of the falls in the tiles
    std::size_t longer = 0;     // of them longer downstream than along the foot
    std::size_t narrowed = 0;   // narrower than the foot of their water falling (a lip beside)
    std::size_t unmatched = 0;  // vertices of foam drawn as no foam of their tile
    std::size_t foams = 0;      // foams near the lip of a fall their pool runs to
    std::size_t far = 0;        // of them out of reach of the falls of some tile they may reach
    std::size_t farNear = 0;    // of those, whose rim comes within kClearLeast of that edge
    std::size_t near = 0;       // of the others, whose rim comes within kClearLeast of that edge
    double closest = std::numeric_limits<double>::infinity(); // meters from the rim to it
    std::size_t twice = 0;      // foams drawn with two shapes or more
};

// How far downstream of the line through the points (px, pz) of the edge of f (lip or foot)
// (x, z) lies, the line interpolated across f at (x, z); none beyond its ends by kSideMargin.
using EdgePoints = std::array<double, terrain::kFallEdge>;

std::optional<double> downstream(const TileFall& f, const EdgePoints& px, const EdgePoints& pz,
                                 double x, double z) {
    const double run = std::hypot(f.footX - f.x, f.footZ - f.z);
    const double dx = (f.footX - f.x) / run;
    const double dz = (f.footZ - f.z) / run;
    EdgePoints across{};
    for (std::size_t p = 0; p < terrain::kFallEdge; ++p) {
        across[p] = (px[p] - f.x) * -dz + (pz[p] - f.z) * dx;
    }
    const double a = (x - f.x) * -dz + (z - f.z) * dx;
    const bool rising = across.back() >= across.front();
    const double low = std::min(across.front(), across.back());
    const double high = std::max(across.front(), across.back());
    if (!(a >= low - kSideMargin) || !(a <= high + kSideMargin)) {
        return std::nullopt;
    }
    std::size_t p = 0;
    while (p + 2 < terrain::kFallEdge && (rising ? across[p + 1] < a : across[p + 1] > a)) {
        ++p;
    }
    const double span = across[p + 1] - across[p];
    const double t = span != 0.0 ? std::clamp((a - across[p]) / span, 0.0, 1.0) : 0.0;
    const double qx = px[p] + t * (px[p + 1] - px[p]);
    const double qz = pz[p] + t * (pz[p + 1] - pz[p]);
    return (x - qx) * dx + (z - qz) * dz;
}

// Meters from (x, z) to the edge of the lip of f, its points joined.
double edgeDistance(const TileFall& f, double x, double z) {
    double nearest = std::numeric_limits<double>::infinity();
    for (std::size_t p = 0; p + 1 < terrain::kFallEdge; ++p) {
        const double ex = f.edgeX[p + 1] - f.edgeX[p];
        const double ez = f.edgeZ[p + 1] - f.edgeZ[p];
        const double length = ex * ex + ez * ez;
        const double t =
            length > 0.0
                ? std::clamp(((x - f.edgeX[p]) * ex + (z - f.edgeZ[p]) * ez) / length, 0.0, 1.0)
                : 0.0;
        nearest = std::min(nearest, std::hypot(x - f.edgeX[p] - t * ex, z - f.edgeZ[p] - t * ez));
    }
    return nearest;
}

bool shaped(const TileFall& f) {
    return f.edge == terrain::kFallEdge && f.top > f.bottom &&
           std::hypot(f.footX - f.x, f.footZ - f.z) > 0.0;
}

// The vertices of the surface of tile t in the pool below a fall, and those a little above it.
void checkPools(const TileData& t, Checks& c) {
    const UnrealWaterMesh m = terrain::unreal::buildTileWater(t);
    for (std::size_t v = 0; v < m.positions.size(); ++v) {
        const double x = t.originX + m.positions[v].x / 100.0;
        const double z = t.originZ + m.positions[v].y / 100.0;
        const double level = t.originY + m.positions[v].z / 100.0;
        for (const TileFall& f : t.falls) {
            const double bottom = static_cast<double>(f.bottom);
            if (!shaped(f) || !(level < 0.5 * (f.top + f.bottom))) {
                continue;
            }
            const std::optional<double> lip = downstream(f, f.edgeX, f.edgeZ, x, z);
            const std::optional<double> foot = downstream(f, f.footEdgeX, f.footEdgeZ, x, z);
            const double pool = f.pool > 0.0f ? static_cast<double>(f.pool) : kPoolLeast;
            if (!lip || !foot || !(*lip > terrain::unreal::kEdgeOver) || !(*foot >= kPastFoot) ||
                !(*foot <= pool) || !(level > bottom - kAbove)) {
                continue;
            }
            ++c.vertices;
            const double above = level - bottom;
            if (above > kFlat && above <= kAbove) {
                ++c.bumps;
                c.highest = std::max(c.highest, above);
            }
            break;
        }
    }
}

// A foam in the world (the world wrapped), by the lip of its fall (centimeters).
using FallKey = std::pair<long long, long long>;
using WorldFoam = std::array<double, 6>; // center x, z, along x, z, its semi-axes

FallKey keyOf(double x, double z) {
    return {std::llround(100.0 * terrain::wrapCoordinate(x, golden::kSmallPlanet.x)),
            std::llround(100.0 * terrain::wrapCoordinate(z, golden::kSmallPlanet.z))};
}

// Meters from the ragged rim of foam s of tile t to the edge of the lip of g, at the nearest.
double rimClear(const TileData& t, const terrain::unreal::FoamShape& s, const TileFall& g) {
    double least = std::numeric_limits<double>::infinity();
    for (int p = 0; p < kRimPoints; ++p) {
        const double angle = 2.0 * M_PI * p / kRimPoints;
        const double u = terrain::unreal::kFoamRim * s.along * std::cos(angle);
        const double v = terrain::unreal::kFoamRim * s.down * std::sin(angle);
        least = std::min(least, edgeDistance(g, t.originX + s.centerX + u * s.alongX - v * s.alongZ,
                                             t.originZ + s.centerZ + u * s.alongZ + v * s.alongX));
    }
    return least;
}

// The foam of fall f of tile t: its shape, how near its rim comes to the lips its pool runs to.
terrain::unreal::FoamShape checkFoam(const TileData& t, const TileFall& f,
                                     const terrain::unreal::FallEdges& edges, Checks& c) {
    const terrain::unreal::FoamShape s = terrain::unreal::foamShape(f, t, edges);
    ++c.shapes;
    c.longer += s.down > s.along + 1e-9 ? 1u : 0u;
    c.narrowed += s.along < s.footHalf ? 1u : 0u;
    const double cx = t.originX + s.centerX;
    const double cz = t.originZ + s.centerZ;
    for (const TileFall& g : t.falls) {
        if (!shaped(g) || !(std::abs(static_cast<double>(g.top - f.bottom)) <= 0.25) ||
            !(g.top < f.top)) {
            continue;
        }
        ++c.foams;
        const double reach = terrain::unreal::kFoamRim * std::max(s.along, s.down);
        const double clear = rimClear(t, s, g);
        if (std::max(std::abs(g.x - cx), std::abs(g.z - cz)) + reach > terrain::kFallNear) {
            ++c.far;
            c.farNear += clear < kClearLeast ? 1u : 0u;
            continue;
        }
        c.closest = std::min(c.closest, clear);
        c.near += clear < kClearLeast ? 1u : 0u;
    }
    return s;
}

// The foams of the falls of tile t, by fall for the comparison between tiles, and the vertices
// of foam its mesh draws: each the shape of a foam of the tile (uv0 its offset, uv2.v its
// downstream semi-axis).
void checkFoams(const TileData& t, Checks& c, std::map<FallKey, std::vector<WorldFoam>>& shapes) {
    const terrain::unreal::FallEdges edges(t);
    std::vector<terrain::unreal::FoamShape> drawn;
    for (const TileFall& f : t.falls) {
        if (!(f.top > f.bottom) || !(std::hypot(f.footX - f.x, f.footZ - f.z) > 0.0)) {
            continue;
        }
        const terrain::unreal::FoamShape s = checkFoam(t, f, edges, c);
        drawn.push_back(s);
        // Compared where it may draw (buildFallCurtains: the tile within its larger semi-axis).
        const double side = terrain::kChunkSizeMeters;
        const double nearX = std::clamp(s.centerX, 0.0, side) - s.centerX;
        const double nearZ = std::clamp(s.centerZ, 0.0, side) - s.centerZ;
        if (!(std::hypot(nearX, nearZ) <= std::max(s.along, s.down))) {
            continue;
        }
        const FallKey center = keyOf(t.originX + s.centerX, t.originZ + s.centerZ);
        shapes[keyOf(f.x, f.z)].push_back({static_cast<double>(center.first),
                                           static_cast<double>(center.second), s.alongX,
                                           s.alongZ, s.along, s.down});
    }
    const UnrealWaterMesh m = terrain::unreal::buildFallCurtains(t);
    for (std::size_t v = 0; v < m.positions.size(); ++v) {
        if (m.uv1[v].u != terrain::unreal::kWaterFoam) {
            continue;
        }
        const double x = m.positions[v].x / 100.0;
        const double z = m.positions[v].y / 100.0;
        bool matched = false;
        for (const terrain::unreal::FoamShape& s : drawn) {
            const std::array<double, 2> o = terrain::unreal::foamOffset(s, x, z);
            matched = matched || (std::abs(s.down - m.uv2[v].v) < 1e-3 &&
                                  std::abs(o[0] - m.uv0[v].u) < 1e-3 &&
                                  std::abs(o[1] - m.uv0[v].v) < 1e-3);
        }
        c.unmatched += matched ? 0u : 1u;
    }
}

// The LOD 0 tiles around the feet of the falls, each once.
std::vector<TileData> tilesAround(const terrain::TerrainTiles& tiles,
                                  const std::vector<terrain::RiverFall>& falls) {
    const auto wrap = [](std::int32_t c, double size) {
        const auto n = static_cast<std::int32_t>(size / terrain::kChunkSizeMeters);
        return ((c % n) + n) % n;
    };
    std::map<std::pair<std::int32_t, std::int32_t>, terrain::TileKey> keys;
    for (const terrain::RiverFall& f : falls) {
        const auto cx = static_cast<std::int32_t>(std::floor(f.footX / terrain::kChunkSizeMeters));
        const auto cz = static_cast<std::int32_t>(std::floor(f.footZ / terrain::kChunkSizeMeters));
        for (int dz = -kAround; dz <= kAround; ++dz) {
            for (int dx = -kAround; dx <= kAround; ++dx) {
                const terrain::ChunkCoord chunk{wrap(cx + dx, golden::kSmallPlanet.x),
                                                wrap(cz + dz, golden::kSmallPlanet.z)};
                keys.emplace(std::make_pair(chunk.x, chunk.z), terrain::tileContaining(chunk, 0));
            }
        }
    }
    std::vector<terrain::TileKey> list;
    for (const auto& [at, key] : keys) {
        list.push_back(key);
    }
    std::vector<TileData> out(list.size());
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        for (std::size_t i = next++; i < list.size(); i = next++) {
            out[i] = tiles.generate({list[i], 0});
        }
    };
    {
        std::vector<std::jthread> workers;
        const unsigned count = std::max(1u, std::thread::hardware_concurrency());
        for (unsigned k = 1; k < count; ++k) {
            workers.emplace_back(work);
        }
        work();
    }
    return out;
}

} // namespace

int main() {
    const falls::World w = falls::makeWorld();
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt,
                                      golden::kSmallPlanet, w.hydrology);
    falls::Reaches reaches(*w.hydrology);
    std::vector<terrain::RiverFall> all;
    for (const std::size_t r : falls::steepReaches(w, reaches)) {
        for (const terrain::RiverFall& f : reaches[r].falls) {
            terrain::RiverFall wrapped = f;
            wrapped.footX = terrain::wrapCoordinate(f.footX, golden::kSmallPlanet.x);
            wrapped.footZ = terrain::wrapCoordinate(f.footZ, golden::kSmallPlanet.z);
            all.push_back(wrapped);
        }
    }
    Checks c;
    std::map<FallKey, std::vector<WorldFoam>> shapes;
    for (const TileData& t : tilesAround(tiles, all)) {
        ++c.tiles;
        checkPools(t, c);
        checkFoams(t, c, shapes);
    }
    for (const auto& [fall, list] : shapes) {
        bool same = true;
        for (const WorldFoam& other : list) {
            for (std::size_t k = 0; k < other.size(); ++k) {
                same = same && std::abs(other[k] - list.front()[k]) <= (k < 2 ? 1.0 : 1e-6);
            }
        }
        c.twice += same ? 0u : 1u;
    }
    std::printf("%zu falls, %zu LOD 0 tiles around their feet: %zu vertices of the surface in "
                "their pools, %zu of them %.2f to %.2f m above the water below their fall "
                "(%.3f m at most)\n",
                all.size(), c.tiles, c.vertices, c.bumps, kFlat, kAbove, c.highest);
    std::printf("%zu foams in the tiles: %zu longer downstream than along the foot, %zu narrower "
                "than the foot of their water falling (a lip beside), %zu vertices of foam not "
                "drawn as their shape, %zu foams drawn with two shapes; %zu near the lip of a fall "
                "their pool runs to (%zu out of reach of the falls of a tile, %zu of them with "
                "their rims within %.1f m of its edge), %zu of the others with their rims within "
                "%.1f m of its edge (%.2f m at the nearest)\n",
                c.shapes, c.longer, c.narrowed, c.unmatched, c.twice, c.foams, c.far, c.farNear,
                kClearLeast, c.near, kClearLeast, c.closest);
    TEST_CHECK(all.size() >= 20 && c.vertices >= 1000 && c.foams >= 10);
    TEST_CHECK(c.bumps == 0);
    TEST_CHECK(c.longer == 0 && c.unmatched == 0 && c.twice == 0);
    TEST_CHECK(c.near == 0);
    return test::result();
}
