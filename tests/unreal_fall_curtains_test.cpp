// The curtains of the falls of the small world as Unreal draws them (phase 12A): for every fall of
// the reaches of the falls, the curtain the LOD 0 tile of its lip draws for it alone
// (buildFallCurtains):
//   - Pieces: one piece per fall, never two with rock between them (a rock in the middle of a
//     lip never splits its curtain).
//   - Width: along the edge of its lip, sampled every kStep meters, the places where the water
//     arrives at the lip (the core: water kWetBack meters upstream at the level above, more than
//     halfway down from it to the level below, over the ground) and may fall (no ground higher
//     than the water above plus kGroundSlack kWetAhead meters in front of the edge, of the foot of
//     its water or halfway): kCoverLeast of them at least under the curtain, all falls together;
//     the curtain kBeyondMost meters at most beyond the first and the last of them.
//   - Ends: no vertex of the two ends of a curtain over dry ground higher than the water above its
//     fall (kGroundSlack meters): the curtain spans the water falling, not the walls of its slot;
//     in its middle, a rock may pierce it (counted).
#include "fall_curtains.hpp"
#include "falls_support.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <vector>

namespace {

using falls::World;
using terrain::TileData;
using terrain::TileFall;
using terrain::unreal::UnrealWaterMesh;

constexpr double kStep = 0.25;
constexpr double kWetBack = 1.0;
constexpr double kWetAhead = 0.5;
constexpr double kGroundSlack = 0.05;
constexpr double kCoverLeast = 0.95;
// The core finds where the water falls from the channel of the course of the fall alone; the
// water of the ground joins every course and may stop a little short of it at a shore (a bank a
// few centimeters over the water, the water of another course standing there).
constexpr double kBeyondMost = 2.5;

struct Checks {
    std::size_t falls = 0;
    std::size_t split = 0;    // falls whose curtain lies in two pieces or more
    double arriving = 0.0;    // meters of the edges where the water arrives and may fall
    double covered = 0.0;     // of them under the curtain
    std::size_t beyond = 0;   // falls whose curtain goes kBeyondMost past that water
    double widest = 0.0;      // meters past it at most
    std::size_t curtain = 0;  // vertices of the curtains
    std::size_t buried = 0;   // of their ends over dry ground above the water of their fall
    std::size_t pierced = 0;  // of their middle so
};

// The point of the edge of f at s meters along it (its points: edgeX, edgeZ), with where its
// water meets the pool (footEdgeX, footEdgeZ).
std::array<double, 4> along(const TileFall& f, double s) {
    for (std::size_t p = 0; p + 1 < terrain::kFallEdge; ++p) {
        const double length = std::hypot(f.edgeX[p + 1] - f.edgeX[p], f.edgeZ[p + 1] - f.edgeZ[p]);
        if (s <= length || p + 2 == terrain::kFallEdge) {
            const double t = length > 0.0 ? std::clamp(s / length, 0.0, 1.0) : 0.0;
            return {f.edgeX[p] + t * (f.edgeX[p + 1] - f.edgeX[p]),
                    f.edgeZ[p] + t * (f.edgeZ[p + 1] - f.edgeZ[p]),
                    f.footEdgeX[p] + t * (f.footEdgeX[p + 1] - f.footEdgeX[p]),
                    f.footEdgeZ[p] + t * (f.footEdgeZ[p + 1] - f.footEdgeZ[p])};
        }
        s -= length;
    }
    return {};
}

// Meters along the edge of f of the point of it nearest to (x, z).
double alongOf(const TileFall& f, double x, double z) {
    double best = std::numeric_limits<double>::infinity();
    double at = 0.0;
    double start = 0.0;
    for (std::size_t p = 0; p + 1 < terrain::kFallEdge; ++p) {
        const double ex = f.edgeX[p + 1] - f.edgeX[p];
        const double ez = f.edgeZ[p + 1] - f.edgeZ[p];
        const double length = std::hypot(ex, ez);
        const double t = length > 0.0 ? std::clamp(((x - f.edgeX[p]) * ex + (z - f.edgeZ[p]) * ez) /
                                                       (length * length),
                                                   0.0, 1.0)
                                       : 0.0;
        const double d = std::hypot(x - f.edgeX[p] - t * ex, z - f.edgeZ[p] - t * ez);
        if (d < best) {
            best = d;
            at = start + t * length;
        }
        start += length;
    }
    return at;
}

// Whether the water arrives at the edge of f at (x, z) and may fall there (the core).
bool falling(const World& w, const TileFall& f, const std::array<double, 4>& e) {
    const double dx = f.footX - f.x;
    const double dz = f.footZ - f.z;
    const double run = std::max(std::hypot(dx, dz), 1e-9);
    const terrain::HeightSample up = w.field.sampleAt(e[0] - kWetBack * dx / run,
                                                      e[1] - kWetBack * dz / run);
    const double top = static_cast<double>(f.top);
    if (up.water == terrain::WaterKind::None || !(up.waterLevel > up.height) ||
        !(up.waterLevel > 0.5 * (f.top + f.bottom))) {
        return false;
    }
    const double wx = e[2] - e[0];
    const double wz = e[3] - e[1];
    const double way = std::max(std::hypot(wx, wz), 1e-9);
    for (const double t : {0.0, 0.5, 1.0}) {
        const terrain::HeightSample h = w.field.sampleAt(e[0] + t * wx + kWetAhead * wx / way,
                                                         e[1] + t * wz + kWetAhead * wz / way);
        if (static_cast<double>(h.height) > top + kGroundSlack) {
            return false;
        }
    }
    return true;
}

// The curtain of fall f of tile t drawn alone: its pieces, the share of the water falling from
// its edge it covers, how far past it it goes, its ends over dry ground.
void checkCurtain(const World& w, const TileData& t, const TileFall& f, Checks& c) {
    TileData alone = t;
    alone.falls = {f};
    const UnrealWaterMesh m = terrain::unreal::buildFallCurtains(alone);
    double from = std::numeric_limits<double>::infinity();
    double to = -std::numeric_limits<double>::infinity();
    std::size_t pieces = 0;
    for (std::size_t v = 0; v < m.positions.size(); ++v) {
        if (m.uv1[v].u != terrain::unreal::kWaterCurtain) {
            continue;
        }
        ++c.curtain;
        const double x = t.originX + m.positions[v].x / 100.0;
        const double z = t.originZ + m.positions[v].y / 100.0;
        const terrain::HeightSample h = w.field.sampleAt(x, z);
        const double above = static_cast<double>(h.height) - static_cast<double>(f.top);
        const bool buried = h.water == terrain::WaterKind::None && above > kGroundSlack;
        const bool end = m.uv0[v].u == 0.0f || m.uv0[v].u == m.uv2[v].v;
        (end ? c.buried : c.pierced) += buried ? 1u : 0u;
        if (m.uv0[v].v == 0.0f) { // the top row, kCurtainAhead in front of the edge
            pieces += m.uv0[v].u == 0.0f ? 1u : 0u;
            from = std::min(from, alongOf(f, x, z));
            to = std::max(to, alongOf(f, x, z));
        }
    }
    double length = 0.0;
    for (std::size_t p = 0; p + 1 < terrain::kFallEdge; ++p) {
        length += std::hypot(f.edgeX[p + 1] - f.edgeX[p], f.edgeZ[p + 1] - f.edgeZ[p]);
    }
    double first = std::numeric_limits<double>::infinity();
    double last = -std::numeric_limits<double>::infinity();
    for (double s = 0.5 * kStep; s < length; s += kStep) {
        if (falling(w, f, along(f, s))) {
            c.arriving += kStep;
            c.covered += s >= from && s <= to ? kStep : 0.0;
            first = std::min(first, s);
            last = std::max(last, s);
        }
    }
    ++c.falls;
    c.split += pieces > 1 ? 1u : 0u;
    const double past = std::max(0.0, std::max(first - from, to - last));
    c.beyond += first <= last && past > kBeyondMost ? 1u : 0u;
    c.widest = first <= last ? std::max(c.widest, past) : c.widest;
}

// The LOD 0 tile of the lip of f and its fall there (the nearest image of its lip).
std::optional<std::pair<TileData, TileFall>> lipTile(const terrain::TerrainTiles& tiles,
                                                     const terrain::RiverFall& f) {
    const double x = terrain::wrapCoordinate(f.x, golden::kSmallPlanet.x);
    const double z = terrain::wrapCoordinate(f.z, golden::kSmallPlanet.z);
    const terrain::TileKey key = terrain::tileContaining(
        {static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
         static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))},
        0);
    TileData t = tiles.generate({key, 0});
    std::optional<TileFall> best;
    double nearest = 1.0;
    for (const TileFall& g : t.falls) {
        const double d = std::hypot(g.x - x, g.z - z);
        if (g.here && g.edge == terrain::kFallEdge && d < nearest) {
            nearest = d;
            best = g;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return std::make_pair(std::move(t), *best);
}

} // namespace

int main() {
    const World w = falls::makeWorld();
    const terrain::TerrainTiles terrainTiles(terrain::kDefaultWorldSeed,
                                             terrain::HeightfieldParams{},
                                             terrain::EcologyParams{}, std::nullopt,
                                             golden::kSmallPlanet, w.hydrology);
    falls::Reaches reaches(*w.hydrology);
    Checks c;
    for (const std::size_t r : falls::steepReaches(w, reaches)) {
        for (const terrain::RiverFall& f : reaches[r].falls) {
            if (const auto lip = lipTile(terrainTiles, f)) {
                checkCurtain(w, lip->first, lip->second, c);
            }
        }
    }
    const double share = c.covered / std::max(c.arriving, 1e-9);
    std::printf("%zu falls: %zu curtains in two pieces or more; the water arriving at their lips "
                "and falling %.0f m, %.0f m of it under their curtains (%.1f %%); %zu curtains "
                "more than %.1f m past it (%.2f m at most); %zu vertices of curtains, %zu of their "
                "ends over dry ground above the water of their fall (%zu of their middle, pierced "
                "by a rock)\n",
                c.falls, c.split, c.arriving, c.covered, 100.0 * share, c.beyond, kBeyondMost,
                c.widest, c.curtain, c.buried, c.pierced);
    TEST_CHECK(c.falls >= 20 && c.curtain >= 1000);
    TEST_CHECK(c.split == 0);
    TEST_CHECK(share >= kCoverLeast);
    TEST_CHECK(c.beyond == 0);
    TEST_CHECK(c.buried == 0);
    return test::result();
}
