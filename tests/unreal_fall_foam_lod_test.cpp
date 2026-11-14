// The foam at the foot of the falls on tiles of every LOD (agent 40, point 3 of the orchestrator):
// with FoamEllipse, the material bounds the foam by the rim of its ellipse alone, read per pixel
// from uv0 (fall_foam.hpp: foamOffset, affine in the position, so exact on triangles of any size),
// no longer by the flag of the border of the cells of its pool, which on the cells of 4 and 8 m of
// the tiles of LOD 2 and 3 drew a corner or a triangle. Its shape is then that of its ellipse
// (foamShape). For the falls of the small world, the tile of LOD 0 to 3 holding the lip of each:
// the center and the semi-axes of its ellipse against those of LOD 0 (printed, and their worst).
#include "fall_edges.hpp"
#include "fall_foam.hpp"
#include "falls_support.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

namespace {

using terrain::TileData;
using terrain::TileFall;

constexpr std::size_t kFalls = 12; // the first falls of the steep reaches, at most
constexpr int kLods = 4;

struct Ellipse {
    double x = 0.0, z = 0.0; // the center, meters in the world
    double along = 0.0, down = 0.0;
};

// The ellipse of the foam of the fall whose lip is at (x, z), on the tile of lod holding it.
std::optional<Ellipse> ellipseAt(const terrain::TerrainTiles& tiles, double x, double z, int lod) {
    const terrain::TileKey key = terrain::tileContaining(
        {static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
         static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))},
        lod);
    const TileData t = tiles.generate({key, 0});
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
    const terrain::unreal::FoamShape s =
        terrain::unreal::foamShape(*best, t, terrain::unreal::FallEdges(t));
    const double ox = key.origin.x * terrain::kChunkSizeMeters;
    const double oz = key.origin.z * terrain::kChunkSizeMeters;
    return Ellipse{s.centerX + ox, s.centerZ + oz, s.along, s.down};
}

} // namespace

int main() {
    const falls::World w = falls::makeWorld();
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt,
                                      golden::kSmallPlanet, w.hydrology);
    falls::Reaches reaches(*w.hydrology);
    std::size_t falls = 0, compared = 0;
    double worstCenter = 0.0, worstAxis = 0.0;
    for (const std::size_t r : falls::steepReaches(w, reaches)) {
        for (const terrain::RiverFall& f : reaches[r].falls) {
            if (falls == kFalls) {
                break;
            }
            const double x = terrain::wrapCoordinate(f.x, golden::kSmallPlanet.x);
            const double z = terrain::wrapCoordinate(f.z, golden::kSmallPlanet.z);
            const std::optional<Ellipse> base = ellipseAt(tiles, x, z, 0);
            if (!base || base->down <= 0.0) {
                continue;
            }
            ++falls;
            for (int lod = 1; lod < kLods; ++lod) {
                const std::optional<Ellipse> e = ellipseAt(tiles, x, z, lod);
                if (!e) {
                    continue;
                }
                ++compared;
                worstCenter = std::max(worstCenter, std::hypot(e->x - base->x, e->z - base->z));
                worstAxis = std::max({worstAxis, std::abs(e->along / base->along - 1.0),
                                      std::abs(e->down / base->down - 1.0)});
            }
        }
    }
    std::printf("%zu falls, %zu tiles of LOD 1 to 3 against LOD 0: the center of the ellipse of "
                "the foam %.2f m off at most, its semi-axes %.1f %% off at most\n",
                falls, compared, worstCenter, 100.0 * worstAxis);
    TEST_CHECK(falls >= 5 && compared >= falls);
    TEST_CHECK(worstCenter < 0.01 && worstAxis < 1e-6); // the same ellipse at every LOD
    return test::result();
}
