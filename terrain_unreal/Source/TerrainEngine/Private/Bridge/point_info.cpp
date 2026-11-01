#include "point_info.hpp"

#include "ground_ray.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/biome.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>

namespace terrain::unreal {

namespace {

// Within this distance of the point, the nearest fall is told.
constexpr double kFallSearchMeters = 1000.0;
// The slopes of the biomes and materials are read 8 m away, as the horizon reads them
// (HorizonParams::materialSlopeStep): the surface the materials of the tiles show.
constexpr double kSlopeStep = 8.0;

std::string format(const char* pattern, ...) {
    std::array<char, 256> text{};
    va_list args;
    va_start(args, pattern);
    std::vsnprintf(text.data(), text.size(), pattern, args);
    va_end(args);
    return text.data();
}

struct Canonical {
    double x = 0.0;
    double z = 0.0;
};

Canonical canonical(const InfoQuery& q) {
    const WorldSize& world = q.field.world();
    return {wrapCoordinate(q.x, world.x), wrapCoordinate(q.z, world.z)};
}

void positionLines(const InfoQuery& q, std::vector<InfoLine>& out) {
    const Canonical c = canonical(q);
    out.push_back({"position", "x, z", format("%.2f, %.2f m", c.x, c.z)});
    const ChunkCoord chunk = worldToChunk(c.x, c.z);
    const auto block = [](double v, std::int32_t first) {
        return static_cast<int>(std::floor(v - first * kChunkSizeMeters));
    };
    out.push_back({"position", "chunk",
                   format("(%d, %d), block (%d, %d)", chunk.x, chunk.z, block(c.x, chunk.x),
                          block(c.z, chunk.z))});
    if (q.y) {
        out.push_back({"position", "altitude", format("%.2f m", *q.y)});
    }
}

void groundLines(const InfoQuery& q, std::vector<InfoLine>& out) {
    const HeightSample s = q.field.sampleAt(q.x, q.z);
    out.push_back({"ground", "ground", format("%.2f m", static_cast<double>(s.height))});
    if (s.water == WaterKind::None) {
        out.push_back({"water", "water", "none"});
    } else {
        out.push_back({"water", "water",
                       format("%s at %.2f m, %.2f m deep", waterKindName(s.water),
                              static_cast<double>(s.waterLevel),
                              static_cast<double>(s.waterLevel - s.height))});
    }
    if (const Hydrology* h = q.field.hydrology()) {
        const double relief = h->relief().heightAt(q.x, q.z);
        out.push_back({"ground", "relief",
                       format("%.2f m before the rivers (%+.2f m)", relief,
                              static_cast<double>(s.height) - relief)});
    }
}

// The site of the column as the horizon builds it (horizon_sampler.cpp groundSite).
SurfaceSite siteOf(const InfoQuery& q, const HeightSample& s) {
    SurfaceSite site;
    site.ground = s.height;
    site.continentalness = s.continentalness;
    const std::array<float, 4> h = {q.field.heightAt(q.x + kSlopeStep, q.z),
                                    q.field.heightAt(q.x - kSlopeStep, q.z),
                                    q.field.heightAt(q.x, q.z + kSlopeStep),
                                    q.field.heightAt(q.x, q.z - kSlopeStep)};
    const double run = 2.0 * kSlopeStep;
    site.gradX = static_cast<float>((static_cast<double>(h[0]) - h[1]) / run);
    site.gradZ = static_cast<float>((static_cast<double>(h[2]) - h[3]) / run);
    float difference = 0.0f;
    for (const float height : h) {
        difference = std::max(difference, std::abs(height - site.ground));
    }
    site.steepness = static_cast<float>(difference / kSlopeStep);
    site.water = s.water;
    site.waterLevel = s.waterLevel;
    return site;
}

void ecologyLines(const InfoQuery& q, std::vector<InfoLine>& out) {
    const HeightSample s = q.field.sampleAt(q.x, q.z);
    const EcologySample e = q.ecology.sampleAt(q.x, q.z, siteOf(q, s));
    out.push_back({"biome", "biome", biomeName(e.biome)});
    out.push_back({"biome", "material", materialName(e.material)});
    out.push_back({"biome", "climate",
                   format("%.1f C, humidity %.2f, slope %.2f",
                          static_cast<double>(e.climate.temperature),
                          static_cast<double>(e.climate.humidity),
                          static_cast<double>(e.climate.slope))});
}

void planetLines(const InfoQuery& q, std::vector<InfoLine>& out) {
    const Hydrology* h = q.field.hydrology();
    if (h == nullptr) {
        out.push_back({"planet", "planet", "no planet file: the relief alone, no river"});
        return;
    }
    const PlanetLevel& planet = h->planet();
    const Canonical c = canonical(q);
    const std::size_t cell = planetCellAt(planet.grid(), c.x, c.z);
    const PlanetSample p = planet.sampleAt(c.x, c.z);
    out.push_back({"planet", "planet cell",
                   format("(%d, %d), #%zu", planet.grid().column(cell), planet.grid().row(cell),
                          cell)});
    out.push_back({"planet", "planet climate",
                   format("%.0f m, %.0f mm/yr of rain, %.1f C%s",
                          static_cast<double>(p.elevation), static_cast<double>(p.precipitation),
                          static_cast<double>(p.temperature), p.frozen ? ", frozen" : "")});
    if (p.ocean) {
        out.push_back({"planet", "planet water", "ocean"});
    } else if (p.lake >= 0) {
        out.push_back({"planet", "planet water", format("lake #%d", p.lake)});
    } else if (p.discharge > 0.0f) {
        out.push_back({"planet", "planet water",
                       format("river of %.0f m3/s, Strahler %d",
                              static_cast<double>(p.discharge), static_cast<int>(p.strahler))});
    }
}

void riverLines(const InfoQuery& q, std::vector<InfoLine>& out) {
    const Hydrology* h = q.field.hydrology();
    if (h == nullptr) {
        return;
    }
    const Canonical c = canonical(q);
    const double relief = h->relief().heightAt(c.x, c.z);
    const RiverCarve carve = h->riversAt(c.x, c.z, relief);
    if (carve.carved) {
        out.push_back({"water", "valley",
                       format("reach #%zu, %s, water at %.2f m, gorge %.2f, mountain %.2f",
                              carve.reach, carve.wet ? "in the channel" : "on its slopes",
                              static_cast<double>(carve.level),
                              static_cast<double>(carve.gorge),
                              static_cast<double>(carve.mountain))});
    }
    const LakeWater lake = h->lakeAt(c.x, c.z, relief);
    if (lake.lake >= 0) {
        out.push_back({"water", "lake",
                       format("#%d, %s at %.2f m", lake.lake, waterKindName(lake.kind),
                              static_cast<double>(lake.level))});
    }
}

void fallLines(const InfoQuery& q, std::vector<InfoLine>& out) {
    const Hydrology* h = q.field.hydrology();
    if (h == nullptr) {
        return;
    }
    const WorldSize& world = q.field.world();
    const Canonical c = canonical(q);
    double nearest = std::numeric_limits<double>::infinity();
    RiverFall best;
    for (const std::size_t r : h->reachesNear(c.x, c.z, kFallSearchMeters)) {
        for (const RiverFall& f : h->reach(r).falls) {
            const double dx = nearestImage(f.x, c.x, world.x) - c.x;
            const double dz = nearestImage(f.z, c.z, world.z) - c.z;
            const double d = std::sqrt(dx * dx + dz * dz);
            if (d < nearest) {
                nearest = d;
                best = f;
            }
        }
    }
    if (nearest > kFallSearchMeters) {
        out.push_back({"water", "nearest fall", "none within 1 km"});
        return;
    }
    out.push_back({"water", "nearest fall",
                   format("%.0f m away, drop %.1f m (%.2f to %.2f), %.0f m wide, lip (%.0f, %.0f)",
                          nearest, static_cast<double>(best.top - best.bottom),
                          static_cast<double>(best.top), static_cast<double>(best.bottom),
                          static_cast<double>(best.width), best.x, best.z)});
}

void coastLines(const InfoQuery& q, std::vector<InfoLine>& out) {
    const Geography* geography = q.field.geography();
    if (geography == nullptr) {
        return;
    }
    const Canonical c = canonical(q);
    const double coast = geography->sampleAt(c.x, c.z).coast;
    out.push_back({"coast", "coast",
                   coast >= 0.0 ? format("%.2f km inland", coast / 1000.0)
                                : format("at sea, %.2f km from the shore", -coast / 1000.0)});
}

// A point hit on the heightmap lies on its surface, within the error of the cells of the tile
// (more on steep ground); off it, the tile is not the heightmap there: the rock of the walls in
// three dimensions, a cave, an edit.
void surfaceLines(const InfoQuery& q, std::vector<InfoLine>& out) {
    if (!q.y) {
        return;
    }
    const double ground = q.field.heightAt(q.x, q.z);
    const double slope = std::max(std::abs(q.field.heightAt(q.x + 1.0, q.z) - ground),
                                  std::abs(q.field.heightAt(q.x, q.z + 1.0) - ground));
    const double tolerance = 1.0 + 2.0 * slope;
    const double off = *q.y - ground;
    out.push_back({"surface", "surface",
                   std::abs(off) <= tolerance
                       ? format("on the heightmap (%+.2f m)", off)
                       : format("off the heightmap by %+.2f m: 3D rock, a cave or an edit",
                                off)});
}

void tileLines(const InfoQuery& q, std::vector<InfoLine>& out) {
    if (q.streamer == nullptr) {
        return;
    }
    const Canonical c = canonical(q);
    const std::optional<TileKey> tile = shownTileAt(*q.streamer, worldToChunk(c.x, c.z));
    out.push_back({"tile", "tile",
                   tile ? format("LOD %d, %.0f m cells, first chunk (%d, %d)", tile->lod,
                                 tileCellMeters(tile->lod), tile->origin.x, tile->origin.z)
                        : std::string("none shown")});
}

} // namespace

std::vector<InfoProvider> defaultInfoProviders() {
    return {positionLines, groundLines, ecologyLines, planetLines, riverLines,
            fallLines,     coastLines,  surfaceLines, tileLines};
}

std::vector<InfoLine> describePoint(const InfoQuery& query,
                                    std::span<const InfoProvider> providers) {
    std::vector<InfoLine> lines;
    for (const InfoProvider& provider : providers) {
        provider(query, lines);
    }
    return lines;
}

const char* waterKindName(WaterKind kind) {
    switch (kind) {
    case WaterKind::None: return "none";
    case WaterKind::Ocean: return "sea";
    case WaterKind::River: return "river";
    case WaterKind::Lake: return "lake";
    case WaterKind::SaltLake: return "salt lake";
    }
    return "unknown";
}

} // namespace terrain::unreal
