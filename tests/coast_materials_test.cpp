// The materials of the coasts in the streamed tiles (agent 33, after the check in Unreal of the
// commit 4 ter), on the default world:
// - No comb at the foot of the walls plunging into the sea: in the tiles of LOD 0 to 3 along the
//   steep wall of a fjord and the walls of a calanque, no texel of the surface blend under the sea
//   is water (drawn as sand under the shallows) where the texel next to it or the one beyond
//   stands a wall at least as steep as 1 above it: bare rock instead (SurfaceSite::bareRock).
//   Before, those texels drew a row of pale points up the foot of the wall, one between each two
//   texels.
// - No seam: two tiles side by side give the same texels on the side they share (the same bits),
//   there and on the reefs.
// - The reefs of bare rock: every texel of the tiles of LOD 0 and 1 over a rock of the reefs
//   (above the water) is rock, no sand, grass or water, down to the water.
#include "coast_planet_support.hpp"
#include "terrain/chunk_coord.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/coast.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::SurfaceMaterial;

// Texels per side of a tile.
constexpr int kTexels = terrain::kChunkCells / terrain::kTileBlendStride + 1;

// The default world with its planet file (coast_planet_support.hpp).
const terrain::TerrainTiles& tiles() {
    return coast_planet::tiles();
}

double heightAt(double x, double z) {
    return static_cast<double>(tiles().heightfield().heightAt(x, z));
}

terrain::TileData tileAt(std::int32_t i, std::int32_t j, int lod) {
    return tiles().generate({terrain::tileContaining({i, j}, lod), 0});
}

// The tiles of a LOD over a square of side meters around (x, z), row by row.
struct Square {
    int lod = 0;
    int n = 0;
    std::vector<terrain::TileData> tiles;
};

Square squareOf(double x, double z, double side, int lod) {
    Square s;
    s.lod = lod;
    const double size = terrain::tileSizeMeters(lod);
    s.n = static_cast<int>(std::ceil(side / size));
    const auto chunks = static_cast<std::int32_t>(terrain::tileChunks(lod));
    const auto i0 = static_cast<std::int32_t>(std::floor((x - 0.5 * side) / size)) * chunks;
    const auto j0 = static_cast<std::int32_t>(std::floor((z - 0.5 * side) / size)) * chunks;
    for (int b = 0; b < s.n; ++b) {
        for (int a = 0; a < s.n; ++a) {
            s.tiles.push_back(tileAt(i0 + a * chunks, j0 + b * chunks, lod));
        }
    }
    return s;
}

terrain::SurfaceBlend texelOf(const terrain::TileData& t, int i, int k) {
    return terrain::unpackBlend(t.surfaceBlend[static_cast<std::size_t>(k * kTexels + i)]);
}

bool weighs(const terrain::SurfaceBlend& b, SurfaceMaterial m) {
    for (std::size_t s = 0; s < b.materials.size(); ++s) {
        if (b.materials[s] == m && b.materialWeights[s] > 0.0f) {
            return true;
        }
    }
    return false;
}

// The texels of water under the sea at the foot of a wall (a texel next to them a pitch higher at
// least, or the one beyond two pitches higher), and the texels that differ between two tiles side
// by side, over the square.
struct Count {
    int under = 0;
    int combs = 0;
    int seams = 0;
};

Count countOf(const Square& s) {
    Count c;
    const double pitch = terrain::kTileBlendStride * terrain::tileCellMeters(s.lod);
    for (int b = 0; b < s.n; ++b) {
        for (int a = 0; a < s.n; ++a) {
            const terrain::TileData& t = s.tiles[static_cast<std::size_t>(b * s.n + a)];
            for (int k = 0; k < kTexels; ++k) {
                for (int i = 0; i < kTexels; ++i) {
                    const terrain::SurfaceBlend blend = texelOf(t, i, k);
                    const double x = t.originX + i * pitch;
                    const double z = t.originZ + k * pitch;
                    if (heightAt(x, z) >= 0.0) {
                        continue;
                    }
                    ++c.under;
                    bool wall = false;
                    for (const auto& [di, dk] : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1},
                                                 {0, -1}}) {
                        for (const double steps : {1.0, 2.0}) {
                            const double h = heightAt(x + di * steps * pitch,
                                                      z + dk * steps * pitch);
                            wall = wall || h >= steps * pitch;
                        }
                    }
                    c.combs += wall && weighs(blend, SurfaceMaterial::Water) ? 1 : 0;
                }
            }
            for (int k = 0; k < kTexels && a + 1 < s.n; ++k) {
                const terrain::TileData& east = s.tiles[static_cast<std::size_t>(b * s.n + a + 1)];
                c.seams += texelOf(t, kTexels - 1, k) == texelOf(east, 0, k) ? 0 : 1;
            }
            for (int i = 0; i < kTexels && b + 1 < s.n; ++i) {
                const terrain::TileData& south =
                    s.tiles[static_cast<std::size_t>((b + 1) * s.n + a)];
                c.seams += texelOf(t, i, kTexels - 1) == texelOf(south, i, 0) ? 0 : 1;
            }
        }
    }
    return c;
}

// The foot of the steep wall of the fjord of the stop points 2 bis and 4 ter, at its mouth, and
// the walls of the reference calanque: the sides of the squares by LOD.
struct Place {
    const char* name;
    double x;
    double z;
};
constexpr std::array<Place, 2> kWalls{{{"fjord", 1'822'300.0, 9'926'500.0},
                                       {"calanque", 23'417'883.0, -2'119'730.0}}};
constexpr std::array<double, 4> kSides{400.0, 600.0, 800.0, 1'024.0};

void testWalls() {
    int combs = 0;
    int seams = 0;
    for (const Place& p : kWalls) {
        for (int lod = 0; lod < 4; ++lod) {
            const Count c = countOf(squareOf(p.x, p.z, kSides[static_cast<std::size_t>(lod)], lod));
            std::printf("%s, LOD %d: %d texels under the sea, %d of water at the foot of a wall, "
                        "%d texels differing between two tiles\n",
                        p.name, lod, c.under, c.combs, c.seams);
            combs += c.combs;
            seams += c.seams;
        }
    }
    TEST_CHECK(combs == 0);
    TEST_CHECK(seams == 0);
}

// The reefs near the cove group of the stop point 2 bis and on two coasts of cliffs.
constexpr std::array<std::array<double, 2>, 3> kReefCoasts{{{23'433'101.0, -2'128'828.0},
                                                            {-15'720'496.0, -6'266'783.0},
                                                            {-26'486'937.0, 7'219'391.0}}};
constexpr std::size_t kRocks = 12;

void testReefs() {
    std::vector<terrain::CoastStack> rocks;
    for (const auto& [cx, cz] : kReefCoasts) {
        for (const terrain::CoastStack& r :
             tiles().heightfield().stacksIn(cx - 5e4, cz - 5e4, cx + 5e4, cz + 5e4)) {
            if (rocks.size() < kRocks) {
                rocks.push_back(r);
            }
        }
    }
    TEST_CHECK(rocks.size() == kRocks);
    int over = 0;
    int soft = 0;
    int seams = 0;
    for (const terrain::CoastStack& r : rocks) {
        for (int lod = 0; lod < 2; ++lod) {
            const Square s = squareOf(r.x, r.z, 2.0 * terrain::tileSizeMeters(lod), lod);
            seams += countOf(s).seams;
            const double pitch = terrain::kTileBlendStride * terrain::tileCellMeters(lod);
            for (const terrain::TileData& t : s.tiles) {
                for (int k = 0; k < kTexels; ++k) {
                    for (int i = 0; i < kTexels; ++i) {
                        const double x = t.originX + i * pitch;
                        const double z = t.originZ + k * pitch;
                        if (std::hypot(x - r.x, z - r.z) > static_cast<double>(r.radius) ||
                            heightAt(x, z) < 0.0) {
                            continue;
                        }
                        ++over;
                        const terrain::SurfaceBlend b = texelOf(t, i, k);
                        soft += b.materials[0] == SurfaceMaterial::Rock &&
                                        b.materialWeights[0] >= 1.0f
                                    ? 0
                                    : 1;
                    }
                }
            }
        }
    }
    std::printf("%zu rocks of the reefs: %d texels over them, %d not all of rock, %d texels "
                "differing between two tiles\n",
                rocks.size(), over, soft, seams);
    TEST_CHECK(over > 0);
    TEST_CHECK(soft == 0);
    TEST_CHECK(seams == 0);
}

} // namespace

int main() {
    testWalls();
    testReefs();
    return test::result();
}
