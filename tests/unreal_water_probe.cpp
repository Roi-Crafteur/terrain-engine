// The water of the tiles as Unreal takes it at given points (agent 27), run by hand: for each
// point (x, z, meters) and each LOD 0 to 3, the tile holding it (no seam) and what the Bridge
// makes of its water — its wet columns, the falls whose lip lies in it (lip, foot, drop, run, mean
// slope, curtain curved or straight), the triangles of its surface (buildTileWater), its cells cut
// at the faces of falls and the vertices faded around them, the triangles of the curtains and of
// the foam (buildFallCurtains); a LOD without a curtain says why. For each fall of the LOD 0 tile,
// three views checked against the ground (heightfield, eye in the air, nothing in between): from
// its pool, from its side, from 120 m above.
// Usage: unreal_water_probe --planet <file> [--seed <seed>] [--threads <n>] <x> <z> [<x> <z>]...

#include "fall_curtains.hpp"
#include "tile_water_mesh.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/storage/planet_file.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using terrain::unreal::UnrealWaterMesh;

constexpr double kDegrees = 57.29577951308232;
constexpr double kTopHeight = 120.0; // meters of the view from above the fall

struct Options {
    std::filesystem::path planet;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    int threads = 0;
    std::vector<std::pair<double, double>> points;
};

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

bool parseOptions(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (i + 1 >= argc) {
            return false;
        }
        if (flag == "--planet") {
            o.planet = argv[++i];
        } else if (flag == "--seed") {
            if (!parse(argv[++i], o.seed)) {
                return false;
            }
        } else if (flag == "--threads") {
            if (!parse(argv[++i], o.threads) || o.threads < 0) {
                return false;
            }
        } else {
            std::pair<double, double> p;
            if (!parse(argv[i], p.first) || !parse(argv[i + 1], p.second)) {
                return false;
            }
            o.points.push_back(p);
            ++i;
        }
    }
    return !o.planet.empty() && !o.points.empty();
}

// The heading of a direction (x, z of the core), degrees clockwise from the north (-z of the
// core, -Y of Unreal; the yaw of Unreal is the heading - 90).
double heading(double dx, double dz) {
    const double h = std::atan2(dx, -dz) * kDegrees;
    return h < 0.0 ? h + 360.0 : h;
}

double groundAt(const terrain::Heightfield& field, double x, double z) {
    return static_cast<double>(field.sampleAt(x, z).height);
}

// Whether the segment from the eye to a point stays above the ground (every 2 m).
bool clearSight(const terrain::Heightfield& field, double ex, double ey, double ez, double tx,
                double ty, double tz) {
    const double length = terrain::dmath::hypot(tx - ex, tz - ez);
    const int steps = std::max(1, static_cast<int>(length / 2.0));
    for (int s = 1; s < steps; ++s) {
        const double t = static_cast<double>(s) / steps;
        if (groundAt(field, ex + t * (tx - ex), ez + t * (tz - ez)) > ey + t * (ty - ey) + 0.3) {
            return false;
        }
    }
    return true;
}

void printView(const terrain::Heightfield& field, const char* name, double ex, double ey, double ez,
               double tx, double ty, double tz, const char* what) {
    const double ground = groundAt(field, ex, ez);
    const double run = terrain::dmath::hypot(tx - ex, tz - ez);
    const double pitch = run > 0.01 ? std::atan2(ty - ey, run) * kDegrees : -90.0;
    const bool clear = ey > ground + 0.5 && clearSight(field, ex, ey, ez, tx, ty, tz);
    const terrain::HeightSample s = field.sampleAt(ex, ez);
    const bool wet = s.water != terrain::WaterKind::None;
    char under[48] = "";
    if (wet) {
        std::snprintf(under, sizeof under, ", water at %.2f m", static_cast<double>(s.waterLevel));
    }
    std::printf("    view %s: point (%.1f ; %.1f), ground %.2f m%s, eye %.2f m (%.1f m up), "
                "heading %.1f, pitch %.1f, target %.1f m away%s: %s\n",
                name, ex, ez, ground, under, ey,
                ey - (wet ? std::max(ground, static_cast<double>(s.waterLevel)) : ground),
                run > 0.01 ? heading(tx - ex, tz - ez) : 0.0, pitch,
                terrain::dmath::hypot(run, ty - ey), clear ? "" : " (SIGHT BLOCKED)", what);
}

// From the pool: the farthest point downstream of the foot, up to 90 m, with its water at the
// level of the pool all the way, the eye 2 m over it, looking at the middle of the curtain.
void frontView(const terrain::Heightfield& field, const terrain::TileFall& f, double dirX,
               double dirZ, double midX, double midY, double midZ) {
    double reach = 2.0;
    for (double d = 4.0; d <= 90.0; d += 2.0) {
        const terrain::HeightSample s = field.sampleAt(f.footX + dirX * d, f.footZ + dirZ * d);
        if (s.water == terrain::WaterKind::None ||
            std::abs(static_cast<double>(s.waterLevel - f.bottom)) > 0.5) {
            break;
        }
        reach = d;
    }
    printView(field, "from the pool", f.footX + dirX * reach, static_cast<double>(f.bottom) + 2.0,
              f.footZ + dirZ * reach, midX, midY, midZ,
              "the curtain from its foot to its lip, the foam on the pool");
}

// From the side: the nearest eye across the fall, at the height of the middle of the curtain or
// higher, in the air with a clear sight of it.
void sideView(const terrain::Heightfield& field, const terrain::TileFall& f, double dirX,
              double dirZ, double midX, double midY, double midZ) {
    const double drop = static_cast<double>(f.top - f.bottom);
    for (double rise = 0.0; rise <= drop + 60.0; rise += 10.0) {
        for (double d = 0.5 * static_cast<double>(f.width) + 10.0; d <= 200.0; d += 5.0) {
            for (const double side : {1.0, -1.0}) {
                const double ex = midX - side * dirZ * d;
                const double ez = midZ + side * dirX * d;
                const double ey = midY + rise;
                if (ey > groundAt(field, ex, ez) + 2.0 &&
                    clearSight(field, ex, ey, ez, midX, midY, midZ)) {
                    printView(field, "from the side", ex, ey, ez, midX, midY, midZ,
                              "the curtain in profile, off the rock behind it");
                    return;
                }
            }
        }
    }
    std::printf("    view from the side: none in the air with a clear sight within 200 m\n");
}

void printViews(const terrain::Heightfield& field, const terrain::TileFall& f) {
    const double dx = f.footX - f.x;
    const double dz = f.footZ - f.z;
    const double run = terrain::dmath::hypot(dx, dz);
    if (!(run > 0.0)) {
        return;
    }
    const double dirX = dx / run;
    const double dirZ = dz / run;
    const double ahead = terrain::unreal::kCurtainAhead + 0.5 * run;
    const double midX = f.x + dirX * ahead;
    const double midZ = f.z + dirZ * ahead;
    const double midY = 0.5 * static_cast<double>(f.top + f.bottom);
    frontView(field, f, dirX, dirZ, midX, midY, midZ);
    sideView(field, f, dirX, dirZ, midX, midY, midZ);
    const double topY = std::max(static_cast<double>(f.top), groundAt(field, midX, midZ));
    printView(field, "from above", midX, topY + kTopHeight, midZ, midX, midY, midZ,
              "the curtain from above, its lip, its foot and the foam (heading: downstream)");
    std::printf("      (from above: turn to heading %.1f, downstream)\n", heading(dirX, dirZ));
}

struct Counts {
    std::size_t curtain = 0;
    std::size_t foam = 0;
    std::size_t faded = 0;
};

Counts count(const UnrealWaterMesh& surface, const UnrealWaterMesh& falls) {
    Counts c;
    for (std::size_t t = 0; t < falls.triangleCount(); ++t) {
        const auto first = static_cast<std::size_t>(falls.triangles[3 * t]);
        (falls.uv1[first].u == terrain::unreal::kWaterCurtain ? c.curtain : c.foam) += 1;
    }
    for (const terrain::unreal::UnrealUV& uv : surface.uv2) {
        c.faded += uv.v == 0.0f ? 1u : 0u;
    }
    return c;
}

void printFall(const terrain::TileFall& f, const terrain::WorldSize& world) {
    const double run = terrain::dmath::hypot(f.footX - f.x, f.footZ - f.z);
    const double drop = static_cast<double>(f.top - f.bottom);
    std::printf("      fall: lip (%.1f ; %.1f) -> foot (%.1f ; %.1f), water %.2f -> %.2f m "
                "(%.2f m), run %.1f m, mean slope %.2f, %.0f m wide: curtain %s, heading %.1f\n",
                terrain::wrapCoordinate(f.x, world.x), terrain::wrapCoordinate(f.z, world.z),
                terrain::wrapCoordinate(f.footX, world.x), terrain::wrapCoordinate(f.footZ, world.z),
                static_cast<double>(f.top), static_cast<double>(f.bottom), drop, run,
                run > 0.0 ? drop / run : 0.0, static_cast<double>(f.width),
                drop >= terrain::unreal::kCurtainCurveSlope * run ? "curved" : "straight",
                heading(f.footX - f.x, f.footZ - f.z));
}

void printTile(const terrain::TerrainTiles& tiles, double x, double z, int lod) {
    const terrain::ChunkCoord chunk =
        terrain::wrapChunk(terrain::worldToChunk(x, z), tiles.world());
    const terrain::TileKey key = terrain::tileContaining(chunk, lod);
    const terrain::TileData data = tiles.generate({key, 0});
    std::size_t wet = 0;
    for (const terrain::WaterColumn& c : data.waterColumns) {
        wet += c.kind == terrain::WaterKind::River || c.kind == terrain::WaterKind::Lake ||
                       c.kind == terrain::WaterKind::SaltLake
                   ? 1u
                   : 0u;
    }
    const UnrealWaterMesh surface = terrain::unreal::buildTileWater(data);
    const UnrealWaterMesh falls = terrain::unreal::buildFallCurtains(data);
    const Counts c = count(surface, falls);
    std::printf("  LOD %d tile (%d, %d), %.0f m cells: %zu wet columns; surface %zu triangles, %d "
                "cells cut, %zu vertices faded; %zu falls: curtains %zu triangles, foam %zu "
                "triangles\n",
                lod, key.origin.x, key.origin.z, terrain::tileCellMeters(lod), wet,
                surface.triangleCount(), surface.cutCells, c.faded, data.falls.size(), c.curtain,
                c.foam);
    if (data.falls.empty()) {
        std::printf("      no curtain: %s\n",
                    data.waterColumns.empty()
                        ? "no lake nor river in this tile"
                        : "no lip of a fall in this tile (the lips near the point lie in its "
                          "neighbors, which draw their curtains)");
    }
    for (const terrain::TileFall& f : data.falls) {
        printFall(f, tiles.world());
    }
    if (lod == 0) {
        for (const terrain::TileFall& f : data.falls) {
            std::printf("    views of the fall of %.2f m:\n", static_cast<double>(f.top - f.bottom));
            printViews(tiles.heightfield(), f);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s --planet <file> [--seed <seed>] [--threads <n>] <x> <z> "
                     "[<x> <z>]...\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    const terrain::WorldSize world;
    const terrain::Heightfield relief(o.seed, terrain::HeightfieldParams{}, world);
    terrain::PlanetParams params;
    params.threads = o.threads;
    bool built = false;
    auto planet = std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
        o.planet, o.seed, terrain::HeightfieldParams{}, params, world, &built));
    std::printf("planet level %s %s\n", built ? "built and written to" : "read from",
                o.planet.string().c_str());
    auto hydrology = std::make_shared<const terrain::Hydrology>(o.seed, relief, planet);
    const terrain::TerrainTiles tiles(o.seed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt, world, hydrology);
    for (const auto& [x, z] : o.points) {
        std::printf("point (%.1f ; %.1f):\n", x, z);
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            printTile(tiles, x, z, lod);
        }
    }
    return EXIT_SUCCESS;
}
