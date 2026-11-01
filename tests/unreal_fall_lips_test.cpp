// The water of the falls of the small world as Unreal draws it around their lips (phase 12A): for
// every fall of the reaches of the falls, in the tiles of LOD 0 to 2 about its lip and its foot
// (the edges of its lip and of its foot, a cell and kAbout meters beyond),
//   - Holes: seen from above, the points of the cut cells of the water (buildTileWater: two
//     corners apart by more than kWaterCutSlope times the cell), 4 each, lie under the water
//     drawn there (its fade above 0: the Edge of the material), a curtain or a foam
//     (buildFallCurtains): no square of bare bed at a lip nor at a foot; kHoleShare of them at
//     most (where the water of the core runs down a steep notch beyond the end of the edge of a
//     lip, too steep to be drawn). The points on the face of a fall (between the edge of its lip
//     and its foot, its water between the water above and below) are its rock where its water
//     does not fall: counted apart, never a hole.
//   - Fades: of the triangles of the water drawn there faded out at their three corners (a cell
//     drawn for nothing), kBareMost at most with nothing over them (water, curtain or foam) where
//     the core has water, off the face of a fall.
//   - Shelves: no triangle of water shown twice as steep as a cut (the face of a fall drawn as
//     water), and kShelfMost at most shown on the face of a fall over the water and the ground
//     of the core there (a shelf of water hanging in the air).
//   - Foam: the foam of the falls (here or near) lies on the water of the pool below one of
//     them, kFoamAbove over it at each of its vertices: never over dry land, a ramp, a lip or
//     another pool.
//   - Curtains: no vertex of a curtain over dry ground higher than the water above its fall
//     (kGroundSlack meters): the curtain spans the water falling, not the walls of its slot.
#include "fall_curtains.hpp"
#include "falls_support.hpp"
#include "tile_water_mesh.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace {

using falls::World;
using terrain::TileData;
using terrain::TileFall;
using terrain::unreal::UnrealWaterMesh;

constexpr int kSide = terrain::kGridSamples;
constexpr int kMostLod = 2;
constexpr double kAbout = 2.0;   // meters about the edges of a fall, beyond a cell
constexpr double kHoleShare = 0.003;
constexpr std::size_t kBareMost = 8;
// Shelves of water over the water and the ground on the face of a fall: the three of e07ffcf
// (a meter over dry ground or over the water of a face, beside a lip) at most.
constexpr std::size_t kShelfMost = 3;
// The foam on the cells of the pool below its fall (fall_curtains.cpp), a few centimeters over
// its water; the ground under a curtain never above the water of its fall by more than
// kGroundSlack.
constexpr double kFoamAbove = 0.05;
constexpr double kLevelLeast = 0.25;
constexpr double kLevelCells = 0.35;
// The steepest a triangle of water shown may be: twice the slope of a cut (a cell not cut is at
// most kWaterCutSlope times sqrt(2), a little more on a stitched side).
constexpr double kSteepest = 2.0 * terrain::unreal::kWaterCutSlope;
constexpr double kGroundSlack = 0.05;

// A triangle seen from above, (x, z) in meters, a value and an altitude (meters) at each corner,
// what it is (UnrealWaterMesh::uv1.u).
struct Triangle {
    std::array<double, 3> x{};
    std::array<double, 3> z{};
    std::array<double, 3> value{};
    std::array<double, 3> y{};
    std::array<double, 4> box{}; // x0, z0, x1, z1
    float kind = 0.0f;
};

// The weights of the corners of t at (x, z) when it lies over it.
std::optional<std::array<double, 3>> weightsOf(const Triangle& t, double x, double z) {
    if (x < t.box[0] || x > t.box[2] || z < t.box[1] || z > t.box[3]) {
        return std::nullopt;
    }
    const auto edge = [&](std::size_t a, std::size_t b) {
        return (t.x[b] - t.x[a]) * (z - t.z[a]) - (t.z[b] - t.z[a]) * (x - t.x[a]);
    };
    const double d0 = edge(0, 1);
    const double d1 = edge(1, 2);
    const double d2 = edge(2, 0);
    const double area = d0 + d1 + d2;
    const bool in = (d0 >= 0.0 && d1 >= 0.0 && d2 >= 0.0) || (d0 <= 0.0 && d1 <= 0.0 && d2 <= 0.0);
    if (!in || std::abs(area) < 1e-12) {
        return std::nullopt;
    }
    return std::array<double, 3>{d1 / area, d2 / area, d0 / area};
}

// The value of t at (x, z) when it lies over it.
std::optional<double> over(const Triangle& t, double x, double z) {
    const std::optional<std::array<double, 3>> w = weightsOf(t, x, z);
    return w ? std::optional<double>((*w)[0] * t.value[0] + (*w)[1] * t.value[1] +
                                     (*w)[2] * t.value[2])
             : std::nullopt;
}

// The steepest slope of t (meters per meter): its normal (a x b) across over up.
double slopeOf(const Triangle& t) {
    const double ax = t.x[1] - t.x[0], ay = t.y[1] - t.y[0], az = t.z[1] - t.z[0];
    const double bx = t.x[2] - t.x[0], by = t.y[2] - t.y[0], bz = t.z[2] - t.z[0];
    const double up = std::abs(az * bx - ax * bz);
    return up > 1e-12 ? std::hypot(ay * bz - az * by, ax * by - ay * bx) / up : 1e9;
}

// The triangles of mesh m of tile t seen from above, value: uv2.v (the fade of the water).
std::vector<Triangle> trianglesOf(const TileData& t, const UnrealWaterMesh& m) {
    std::vector<Triangle> out;
    for (std::size_t n = 0; n + 2 < m.triangles.size(); n += 3) {
        Triangle tri;
        for (std::size_t c = 0; c < 3; ++c) {
            const auto v = static_cast<std::size_t>(m.triangles[n + c]);
            tri.x[c] = t.originX + m.positions[v].x / terrain::unreal::kCentimetersPerMeter;
            tri.z[c] = t.originZ + m.positions[v].y / terrain::unreal::kCentimetersPerMeter;
            tri.value[c] = m.uv2[v].v;
            tri.y[c] = t.originY + m.positions[v].z / terrain::unreal::kCentimetersPerMeter;
        }
        tri.kind = m.uv1[static_cast<std::size_t>(m.triangles[n])].u;
        const auto [x0, x1] = std::minmax({tri.x[0], tri.x[1], tri.x[2]});
        const auto [z0, z1] = std::minmax({tri.z[0], tri.z[1], tri.z[2]});
        tri.box = {x0, z0, x1, z1};
        out.push_back(tri);
    }
    return out;
}

// A tile of the small world and its water as Unreal draws it.
struct Drawn {
    TileData data;
    std::vector<Triangle> water;
    std::vector<Triangle> cover; // curtains and foams
    UnrealWaterMesh falls;
};

using Key = std::pair<std::array<std::int32_t, 2>, int>;

class Tiles {
public:
    explicit Tiles(const terrain::TerrainTiles& tiles) : tiles_(tiles) {}

    const Drawn& at(double x, double z, int lod) {
        x = terrain::wrapCoordinate(x, golden::kSmallPlanet.x);
        z = terrain::wrapCoordinate(z, golden::kSmallPlanet.z);
        const terrain::TileKey key = terrain::tileContaining(
            {static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
             static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))},
            lod);
        const Key k{{key.origin.x, key.origin.z}, lod};
        auto it = drawn_.find(k);
        if (it == drawn_.end()) {
            Drawn d{tiles_.generate({key, 0}), {}, {}, {}};
            d.water = trianglesOf(d.data, terrain::unreal::buildTileWater(d.data));
            d.falls = terrain::unreal::buildFallCurtains(d.data);
            d.cover = trianglesOf(d.data, d.falls);
            it = drawn_.emplace(k, std::move(d)).first;
        }
        return it->second;
    }

private:
    const terrain::TerrainTiles& tiles_;
    std::map<Key, Drawn> drawn_;
};

struct Checks {
    std::size_t falls = 0;
    std::size_t points = 0;
    std::size_t holes = 0, faceHoles = 0; // and on the rock of a face
    std::size_t faded = 0;
    std::size_t fadedBare = 0, faceBare = 0; // nothing over them, and on the rock of a face
    std::size_t steep = 0;     // triangles of water shown twice as steep as a cut
    std::size_t faceWater = 0; // triangles of water shown in the air on the face of a fall
    std::size_t foam = 0, foamOff = 0; // vertices of the foams, not on the pool below a fall
    std::size_t curtain = 0; // vertices of the curtains
    std::size_t buried = 0;  // of them over dry ground above the water of their fall
    std::set<Key> seen;      // the tiles whose foams and curtains were checked
    std::set<std::pair<const Drawn*, std::size_t>> fadedSeen;
};

// The box (x0, z0, x1, z1) of the edges of fall f (its lip and its foot), wrapped.
std::array<double, 4> boxOf(const terrain::RiverFall& f) {
    std::array<double, 4> box{f.x, f.z, f.x, f.z};
    const auto add = [&](double x, double z) {
        box = {std::min(box[0], x), std::min(box[1], z), std::max(box[2], x), std::max(box[3], z)};
    };
    add(f.footX, f.footZ);
    for (std::size_t p = 0; p < f.edge; ++p) {
        add(f.edgeX[p], f.edgeZ[p]);
        add(f.footEdgeX[p], f.footEdgeZ[p]);
    }
    return box;
}

// The levels of the corners of cell (i, k) of t as buildTileWater cuts it (a dry corner at the
// mean of the wet ones), or none when it is dry.
std::optional<std::array<double, 4>> levelsOf(const TileData& t, int i, int k) {
    std::array<double, 4> levels{};
    std::array<bool, 4> wet{};
    double sum = 0.0;
    int wets = 0;
    for (int c = 0; c < 4; ++c) {
        const auto at = static_cast<std::size_t>((k + c / 2) * kSide + i + c % 2);
        const terrain::WaterColumn& w = t.waterColumns[at];
        wet[static_cast<std::size_t>(c)] = w.kind != terrain::WaterKind::None;
        levels[static_cast<std::size_t>(c)] = w.level;
        sum += wet[static_cast<std::size_t>(c)] ? static_cast<double>(w.level) : 0.0;
        wets += wet[static_cast<std::size_t>(c)] ? 1 : 0;
    }
    if (wets == 0) {
        return std::nullopt;
    }
    for (std::size_t c = 0; c < 4; ++c) {
        levels[c] = wet[c] ? levels[c] : sum / wets;
    }
    return levels;
}

bool cut(const std::array<double, 4>& l, double spacing) {
    const double limit = terrain::unreal::kWaterCutSlope * spacing;
    return std::abs(l[0] - l[1]) > limit || std::abs(l[2] - l[3]) > limit ||
           std::abs(l[0] - l[2]) > limit || std::abs(l[1] - l[3]) > limit;
}

// Whether (x, z) is under the water of a tile (its fade above 0), a curtain or a foam.
bool covered(const std::vector<const Drawn*>& around, double x, double z) {
    for (const Drawn* d : around) {
        for (const Triangle& t : d->water) {
            const std::optional<double> fade = over(t, x, z);
            if (fade && *fade > 0.0) {
                return true;
            }
        }
        for (const Triangle& t : d->cover) {
            if (over(t, x, z)) {
                return true;
            }
        }
    }
    return false;
}

// The distance from (x, z) to the quadrilateral q (its corners around it; 0 inside).
double toQuad(const std::array<std::array<double, 2>, 4>& q, double x, double z) {
    bool in = false;
    double nearest = std::numeric_limits<double>::infinity();
    for (std::size_t a = 0, b = 3; a < 4; b = a++) {
        const double ax = q[a][0], az = q[a][1], bx = q[b][0], bz = q[b][1];
        if ((az > z) != (bz > z) && x < ax + (bx - ax) * (z - az) / (bz - az)) {
            in = !in;
        }
        const double ex = bx - ax, ez = bz - az, length = ex * ex + ez * ez;
        const double t =
            length > 0.0 ? std::clamp(((x - ax) * ex + (z - az) * ez) / length, 0.0, 1.0) : 0.0;
        nearest = std::min(nearest, std::hypot(x - ax - t * ex, z - az - t * ez));
    }
    return in ? 0.0 : nearest;
}

// Whether (x, z) at level lies on the face of a fall of t (here or near): more than tolerance
// meters over the water below it and under the water above it, between the edge of its lip and
// where its water meets the pool (margin meters beyond them at most): its rock, which its curtain
// covers where its water falls and leaves bare beside it.
bool onFace(const TileData& t, double x, double z, double level, double tolerance,
            double margin) {
    for (const TileFall& f : t.falls) {
        if (!(level > static_cast<double>(f.bottom) + tolerance) ||
            !(level < static_cast<double>(f.top) - tolerance)) {
            continue;
        }
        const double dx = f.footX - f.x, dz = f.footZ - f.z;
        const double run = std::max(std::hypot(dx, dz), 1e-9);
        std::array<std::array<double, 2>, terrain::kFallEdge> lip{}, foot{};
        for (std::size_t p = 0; p < terrain::kFallEdge; ++p) {
            const double w = 0.5 * static_cast<double>(f.width) *
                             (2.0 * static_cast<double>(p) / (terrain::kFallEdge - 1.0) - 1.0);
            const bool shaped = f.edge == terrain::kFallEdge;
            lip[p] = shaped ? std::array<double, 2>{f.edgeX[p], f.edgeZ[p]}
                            : std::array<double, 2>{f.x - dz / run * w, f.z + dx / run * w};
            foot[p] = shaped ? std::array<double, 2>{f.footEdgeX[p], f.footEdgeZ[p]}
                             : std::array<double, 2>{lip[p][0] + dx, lip[p][1] + dz};
        }
        for (std::size_t p = 0; p + 1 < terrain::kFallEdge; ++p) {
            if (toQuad({lip[p], lip[p + 1], foot[p + 1], foot[p]}, x, z) <= margin) {
                return true;
            }
        }
    }
    return false;
}

// The points of the cut cells of tile d within box (meters), against what covers them.
void checkHoles(const Drawn& d, const std::vector<const Drawn*>& around,
                const std::array<double, 4>& box, int lod, Checks& c) {
    const double spacing = terrain::tileCellMeters(lod);
    const double tolerance = kLevelLeast + kLevelCells * spacing;
    const TileData& t = d.data;
    if (t.waterColumns.empty()) {
        return; // no water
    }
    for (int k = 0; k + 1 < kSide; ++k) {
        for (int i = 0; i + 1 < kSide; ++i) {
            const double x = t.originX + (i + 0.5) * spacing;
            const double z = t.originZ + (k + 0.5) * spacing;
            if (x < box[0] || x > box[2] || z < box[1] || z > box[3]) {
                continue;
            }
            const std::optional<std::array<double, 4>> levels = levelsOf(t, i, k);
            if (!levels || !cut(*levels, spacing)) {
                continue;
            }
            const std::array<double, 4>& l = *levels;
            for (const double u : {0.25, 0.75}) {
                for (const double v : {0.25, 0.75}) {
                    ++c.points;
                    const double px = t.originX + (i + u) * spacing;
                    const double pz = t.originZ + (k + v) * spacing;
                    if (covered(around, px, pz)) {
                        continue;
                    }
                    const double level = (1.0 - v) * ((1.0 - u) * l[0] + u * l[1]) +
                                         v * ((1.0 - u) * l[2] + u * l[3]);
                    ++(onFace(t, px, pz, level, tolerance, spacing) ? c.faceHoles : c.holes);
                }
            }
        }
    }
}

// The triangles of the water of d within box: those shown on the face of a fall over the water
// and the ground of the core there (a shelf of water hanging from it) or twice as steep as a cut
// (the face drawn as water), and those faded out at their three corners (two for a cell) with
// nothing else over their middle (no water that shows, no curtain, no foam) where the core has
// water, on the face of a fall (its rock, beside its curtain) or not.
void checkFades(const World& w, const Drawn& d, const std::vector<const Drawn*>& around,
                const std::array<double, 4>& box, Checks& c) {
    const double spacing = terrain::tileCellMeters(d.data.request.key.lod);
    const double tolerance = kLevelLeast + kLevelCells * spacing;
    for (std::size_t n = 0; n < d.water.size(); ++n) {
        const Triangle& t = d.water[n];
        const double x = (t.x[0] + t.x[1] + t.x[2]) / 3.0;
        const double z = (t.z[0] + t.z[1] + t.z[2]) / 3.0;
        if (x < box[0] || x > box[2] || z < box[1] || z > box[3] ||
            !c.fadedSeen.insert({&d, n}).second) {
            continue;
        }
        const bool shown = t.value[0] > 0.0 || t.value[1] > 0.0 || t.value[2] > 0.0;
        const double y = (t.y[0] + t.y[1] + t.y[2]) / 3.0;
        c.steep += shown && slopeOf(t) > kSteepest ? 1u : 0u;
        if (shown && onFace(d.data, x, z, y, tolerance, spacing)) {
            // Over the water of the core there (or over dry ground) and over its ground.
            const terrain::HeightSample h = w.field.sampleAt(x, z);
            const bool off = h.water == terrain::WaterKind::None ||
                             y > static_cast<double>(h.waterLevel) + tolerance;
            c.faceWater += off && y > static_cast<double>(h.height) ? 1u : 0u;
        }
        if (!shown) {
            ++c.faded;
            const terrain::HeightSample h = w.field.sampleAt(x, z);
            if (h.water != terrain::WaterKind::None && !covered(around, x, z)) {
                const bool face = onFace(d.data, x, z, static_cast<double>(h.waterLevel),
                                         tolerance, spacing);
                ++(face ? c.faceBare : c.fadedBare);
            }
        }
    }
}

// The foam of the falls of d (here or near): each vertex kFoamAbove over the water drawn at its
// column (drawnWaterLevels), that water the pool below one of them (within kLevelLeast plus
// kLevelCells cells of its level); never over dry land, a ramp or another pool.
void checkFoams(const Drawn& d, Checks& c) {
    const std::vector<double> levels = terrain::unreal::drawnWaterLevels(d.data);
    const double spacing = terrain::tileCellMeters(d.data.request.key.lod);
    const double tolerance = kLevelLeast + kLevelCells * spacing + 1e-6;
    const UnrealWaterMesh& m = d.falls;
    for (std::size_t v = 0; v < m.positions.size(); ++v) {
        if (m.uv1[v].u != terrain::unreal::kWaterFoam) {
            continue;
        }
        ++c.foam;
        const auto i = static_cast<int>(std::lround(m.positions[v].x / 100.0 / spacing));
        const auto k = static_cast<int>(std::lround(m.positions[v].y / 100.0 / spacing));
        const double level =
            levels.empty() ? -1e9 : levels[static_cast<std::size_t>(k * kSide + i)];
        const double altitude = d.data.originY + m.positions[v].z / 100.0;
        bool pool = false;
        for (const TileFall& f : d.data.falls) {
            pool = pool || std::abs(level - static_cast<double>(f.bottom)) <= tolerance;
        }
        c.foamOff += pool && std::abs(altitude - level - kFoamAbove) < 1e-3 ? 0u : 1u;
    }
}

// The curtains of the falls whose lip lies in d, each drawn alone: none of their vertices over
// dry ground higher than the water above the fall (kGroundSlack meters).
void checkCurtains(const World& w, const Drawn& d, Checks& c) {
    for (const TileFall& f : d.data.falls) {
        if (!f.here) {
            continue;
        }
        TileData alone = d.data;
        alone.falls = {f};
        const UnrealWaterMesh m = terrain::unreal::buildFallCurtains(alone);
        for (std::size_t v = 0; v < m.positions.size(); ++v) {
            if (m.uv1[v].u != terrain::unreal::kWaterCurtain) {
                continue;
            }
            ++c.curtain;
            const terrain::HeightSample h =
                w.field.sampleAt(d.data.originX + m.positions[v].x / 100.0,
                                 d.data.originZ + m.positions[v].y / 100.0);
            const double above = static_cast<double>(h.height) - static_cast<double>(f.top);
            const bool buried = h.water == terrain::WaterKind::None && above > kGroundSlack;
            c.buried += buried ? 1u : 0u;
        }
    }
}

void checkFall(const World& w, Tiles& tiles, const terrain::RiverFall& f, Checks& c) {
    ++c.falls;
    for (int lod = 0; lod <= kMostLod; ++lod) {
        const double spacing = terrain::tileCellMeters(lod);
        const double about = spacing + kAbout;
        std::array<double, 4> box = boxOf(f);
        box = {box[0] - about, box[1] - about, box[2] + about, box[3] + about};
        // The tiles over the box (the lip and the foot in one or a few).
        const double side = terrain::tileSizeMeters(lod);
        std::vector<const Drawn*> around;
        std::set<const Drawn*> taken;
        for (double z = box[1]; z < box[3] + side; z += side) {
            for (double x = box[0]; x < box[2] + side; x += side) {
                const Drawn& d = tiles.at(std::min(x, box[2]), std::min(z, box[3]), lod);
                if (taken.insert(&d).second) {
                    around.push_back(&d);
                }
            }
        }
        // The box at the images of the tiles (wrapped as they are).
        const double shiftX = terrain::wrapCoordinate(f.x, golden::kSmallPlanet.x) - f.x;
        const double shiftZ = terrain::wrapCoordinate(f.z, golden::kSmallPlanet.z) - f.z;
        box = {box[0] + shiftX, box[1] + shiftZ, box[2] + shiftX, box[3] + shiftZ};
        for (const Drawn* d : around) {
            checkHoles(*d, around, box, lod, c);
            checkFades(w, *d, around, box, c);
            const Key k{{d->data.request.key.origin.x, d->data.request.key.origin.z}, lod};
            if (c.seen.insert(k).second) {
                checkFoams(*d, c);
                checkCurtains(w, *d, c);
            }
        }
    }
}

} // namespace

int main() {
    const World w = falls::makeWorld();
    const terrain::TerrainTiles terrainTiles(terrain::kDefaultWorldSeed,
                                             terrain::HeightfieldParams{},
                                             terrain::EcologyParams{}, std::nullopt,
                                             golden::kSmallPlanet, w.hydrology);
    falls::Reaches reaches(*w.hydrology);
    Tiles tiles(terrainTiles);
    Checks c;
    for (const std::size_t r : falls::steepReaches(w, reaches)) {
        for (const terrain::RiverFall& f : reaches[r].falls) {
            checkFall(w, tiles, f, c);
        }
    }
    const double points = static_cast<double>(std::max<std::size_t>(c.points, 1));
    std::printf("%zu falls, LOD 0 to %d: %zu points of the cut cells about them, %zu holes (no "
                "water, curtain nor foam over them, %.1f per thousand) and %zu on the rock of a "
                "face beside its curtain; %zu tiles, %zu triangles of their water shown in the air "
                "on the face of a fall, %zu twice as steep as a cut, %zu drawn faded out at their "
                "three corners, %zu of them with nothing over the water of the core (%zu more on "
                "the face of a fall)\n",
                c.falls, kMostLod, c.points, c.holes,
                1000.0 * static_cast<double>(c.holes) / points, c.faceHoles, c.seen.size(),
                c.faceWater, c.steep, c.faded, c.fadedBare, c.faceBare);
    std::printf("%zu vertices of foam, %zu not on the water of a pool below a fall; %zu vertices "
                "of curtains, %zu over dry ground above the water of their fall\n",
                c.foam, c.foamOff, c.curtain, c.buried);
    TEST_CHECK(c.falls >= 20 && c.points >= 1000 && c.foam >= 1000 && c.curtain >= 1000);
    TEST_CHECK(static_cast<double>(c.holes) <= kHoleShare * points);
    TEST_CHECK(c.fadedBare <= kBareMost);
    TEST_CHECK(c.faceWater <= kShelfMost);
    TEST_CHECK(c.steep == 0);
    TEST_CHECK(c.foamOff == 0);
    TEST_CHECK(c.buried == 0);
    return test::result();
}
