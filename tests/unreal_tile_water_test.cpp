// The water of the tiles as Unreal draws it (agent 27): the surface of their lakes and rivers
// (buildTileWater) and the curtains and foam of their falls (buildFallCurtains). On made-up tiles:
// a lake flat at its level, its dry corners carried under the bank, the sea left out, a fall cut,
// the odd columns of a side stitched to a coarser tile on its edge, front faces up. On the small
// world with its water: the common side of two tiles of a LOD, the side of a tile against a
// coarser one, the side of a LOD 3 tile against the samples of the horizon (the water texels,
// bit for bit), the curtains from the lip to the foot of their falls; and the time it takes.
#include "fall_curtains.hpp"
#include "horizon_textures.hpp"
#include "tile_mesh_conversion.hpp"
#include "tile_water_mesh.hpp"
#include "water_level_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace {

using terrain::TileData;
using terrain::WaterColumn;
using terrain::WaterKind;
using terrain::unreal::UnrealVector3f;
using terrain::unreal::UnrealWaterMesh;
using water_level::tileAt;
using water_level::World;

constexpr int kSide = terrain::kGridSamples;
constexpr int kLast = kSide - 1;

TileData madeUp(int lod, unsigned seams) {
    TileData t;
    t.request = {{{0, 0}, lod}, seams};
    t.originY = 96.0;
    t.waterColumns.resize(static_cast<std::size_t>(kSide * kSide));
    return t;
}

WaterColumn& column(TileData& t, int i, int k) {
    return t.waterColumns[static_cast<std::size_t>(k * kSide + i)];
}

// The z of the front face of a triangle as Unreal draws it: (c - a) x (b - a).
double frontZ(const UnrealWaterMesh& m, std::size_t t) {
    const UnrealVector3f& a = m.positions[static_cast<std::size_t>(m.triangles[3 * t])];
    const UnrealVector3f& b = m.positions[static_cast<std::size_t>(m.triangles[3 * t + 1])];
    const UnrealVector3f& c = m.positions[static_cast<std::size_t>(m.triangles[3 * t + 2])];
    return static_cast<double>(c.x - a.x) * (b.y - a.y) -
           static_cast<double>(c.y - a.y) * (b.x - a.x);
}

// A lake over the columns i <= 8 at 100.25 m: every vertex at its level, the cells up to the
// column 9 drawn (the dry corners of the last ones carried at the level), front faces up; the
// sea and dry land give nothing.
void testLake() {
    TileData t = madeUp(0, 0);
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i <= 8; ++i) {
            column(t, i, k) = {100.25f, WaterKind::Lake, 0.0f, 0.0f};
        }
        column(t, 12, k) = {0.0f, WaterKind::Ocean, 0.0f, 0.0f};
    }
    const UnrealWaterMesh m = terrain::unreal::buildTileWater(t);
    bool flat = true;
    float reach = 0.0f;
    for (std::size_t n = 0; n < m.positions.size(); ++n) {
        flat = flat && m.positions[n].z == 425.0f && m.uv1[n].u == terrain::unreal::kWaterLake &&
               m.uv0[n].u == 0.0f && m.normals[n].z == 1.0f && m.uv2[n].u == 100.25f;
        reach = std::max(reach, m.positions[n].x);
    }
    bool up = true;
    for (std::size_t n = 0; n < m.triangleCount(); ++n) {
        up = up && frontZ(m, n) > 0.0;
    }
    TEST_CHECK(flat && up && reach == 900.0f && m.triangleCount() == 2u * 9u * 16u);
    TileData sea = madeUp(0, 0);
    for (WaterColumn& c : sea.waterColumns) {
        c = {0.0f, WaterKind::Ocean, 0.0f, 0.0f};
    }
    TEST_CHECK(terrain::unreal::buildTileWater(sea).empty());
    TEST_CHECK(terrain::unreal::buildTileWater(TileData{}).empty());
    std::printf("lake: %zu vertices, %zu triangles, %s, faces %s\n", m.positions.size(),
                m.triangleCount(), flat ? "flat at its level" : "NOT FLAT", up ? "up" : "DOWN");
}

// Whether no drawn cell is steeper than the cut between two of its corners (a diagonal: twice).
bool noSteepCell(const UnrealWaterMesh& m) {
    bool gentle = true;
    for (std::size_t n = 0; n < m.triangleCount(); ++n) {
        for (std::size_t a = 0; a < 3; ++a) {
            const UnrealVector3f& p = m.positions[static_cast<std::size_t>(m.triangles[3 * n + a])];
            const UnrealVector3f& q =
                m.positions[static_cast<std::size_t>(m.triangles[3 * n + (a + 1) % 3])];
            const double run = std::abs(double{p.x} - q.x) + std::abs(double{p.y} - q.y);
            gentle = gentle && std::abs(double{p.z} - q.z) <= terrain::unreal::kWaterCutSlope * run +
                                                               1e-2;
        }
    }
    return gentle;
}

// A river down z, falling 10 m between the rows 8 and 9: no cell across the fall, the 16 cells
// there cut and the last one of the next row (on the side stitched to a coarser tile, +x, the
// column of row 9 stands halfway down the fall), the edge 0 on their corners and 1 elsewhere; on
// that side, the odd columns at the mean level of their even neighbors, on the straight edge of
// its cells, while the side not stitched (-x) keeps its levels.
void testFallAndSeam() {
    TileData t = madeUp(1, terrain::kSeamPosX);
    const auto level = [](int k) {
        return 200.0f - 0.01f * static_cast<float>(k * k) - (k > 8 ? 10.0f : 0.0f);
    };
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i < kSide; ++i) {
            column(t, i, k) = {level(k), WaterKind::River, 0.0f, 1.0f};
        }
    }
    const UnrealWaterMesh m = terrain::unreal::buildTileWater(t);
    bool cut = true;
    for (std::size_t n = 0; n < m.triangleCount(); ++n) {
        float low = 1e9f;
        float high = -1e9f;
        for (std::size_t c = 0; c < 3; ++c) {
            const float z = m.positions[static_cast<std::size_t>(m.triangles[3 * n + c])].z;
            low = std::min(low, z);
            high = std::max(high, z);
        }
        cut = cut && high - low < 500.0f;
    }
    bool stitched = true;
    bool kept = true;
    bool edges = m.cutCells == 17 && noSteepCell(m);
    for (std::size_t n = 0; n < m.positions.size(); ++n) {
        const UnrealVector3f& p = m.positions[n];
        const int k = static_cast<int>(std::lround(p.y / 200.0f));
        const bool rim = k == 8 || k == 9 || (k == 10 && p.x >= 3000.0f);
        edges = edges && m.uv2[n].v == (rim ? 0.0f : 1.0f);
        const bool side = p.x == 3200.0f;
        const double expected =
            side && k % 2 == 1 ? 0.5 * (double{level(k - 1)} + level(k + 1)) : level(k);
        const bool at = std::abs(p.z - 100.0 * (expected - 96.0)) < 2e-3;
        stitched = stitched && (!side || at) && m.uv0[n].v == 1.0f;
        kept = kept && (side || at);
    }
    TEST_CHECK(cut && stitched && kept && edges && !m.empty());
    std::printf("river: %zu triangles, %s (%d cells cut, %s), side stitched to the coarser tile "
                "%s, other sides %s\n",
                m.triangleCount(), cut ? "no cell across the fall" : "A CELL ACROSS THE FALL",
                m.cutCells, edges ? "their corners faded" : "WRONG EDGES",
                stitched ? "on its edge" : "OFF ITS EDGE", kept ? "at their levels" : "MOVED");
}

// The Unreal Z of the water at the column (i, k) of a tile, in meters, or none.
std::optional<double> waterAt(const TileData& t, int i, int k) {
    const WaterColumn& c = t.waterColumns.empty()
                               ? WaterColumn{}
                               : t.waterColumns[static_cast<std::size_t>(k * kSide + i)];
    if (c.kind != WaterKind::River && c.kind != WaterKind::Lake && c.kind != WaterKind::SaltLake) {
        return std::nullopt;
    }
    const UnrealWaterMesh m = terrain::unreal::buildTileWater(t);
    const double spacing = terrain::tileCellMeters(t.request.key.lod);
    for (const UnrealVector3f& p : m.positions) {
        if (std::abs(p.x - 100.0 * i * spacing) < 1e-3 &&
            std::abs(p.y - 100.0 * k * spacing) < 1e-3) {
            return t.originY + p.z / 100.0;
        }
    }
    return std::nullopt;
}

// Around the lips of falls: the common side of two tiles of LOD 0 holds the same water; the side
// of a LOD 0 tile against a LOD 1 tile lies on its edge; the side of a LOD 3 tile has the levels
// of the water texels of the horizon level 0, bit for bit.
void testSides(const World& w, const std::vector<terrain::RiverFall>& falls) {
    std::size_t same = 0;
    std::size_t edge = 0;
    std::size_t texels = 0;
    double worstSame = 0.0;
    double worstEdge = 0.0;
    bool exact = true;
    for (const terrain::RiverFall& f : falls) {
        const TileData a = tileAt(w.tiles, f.x, f.z, 0);
        const TileData b = tileAt(w.tiles, a.originX + 16.0, a.originZ, 0);
        TileData fine = a;
        fine.request.seams = terrain::kSeamPosX;
        const TileData coarse = tileAt(w.tiles, a.originX + 16.0, a.originZ, 1);
        const int row = static_cast<int>(std::lround((a.originZ - coarse.originZ) / 2.0));
        for (int k = 0; k < kSide; ++k) {
            const auto left = waterAt(a, kLast, k);
            const auto right = waterAt(b, 0, k);
            if (left && right) {
                ++same;
                worstSame = std::max(worstSame, std::abs(*left - *right));
            }
            const auto low = waterAt(coarse, 0, row + k / 2);
            const auto high = waterAt(coarse, 0, row + (k + 1) / 2);
            const auto mid = waterAt(fine, kLast, k);
            if (low && high && mid && coarse.originX == a.originX + 16.0) {
                ++edge;
                worstEdge = std::max(worstEdge, std::abs(*mid - 0.5 * (*low + *high)));
            }
        }
        const TileData far = tileAt(w.tiles, f.x, f.z, 3);
        for (int k = 0; k < kSide && !far.waterColumns.empty(); ++k) {
            const double x = far.originX;
            const double z = far.originZ + 8.0 * k;
            const terrain::HorizonIndex s = terrain::canonicalCenter(w.horizon.params(), 0, x, z);
            const terrain::HorizonCell cell =
                w.horizon.sample(0, {s.x, s.z, s.x + 1, s.z + 1}).front();
            const WaterColumn& c = far.waterColumns[static_cast<std::size_t>(k * kSide)];
            if (terrain::samplePosition(s.x, 8) == x && terrain::samplePosition(s.z, 8) == z &&
                c.level > terrain::kNoWaterLevel) {
                ++texels;
                exact = exact &&
                        water_level::same(terrain::unreal::waterTexel(cell.waterLevel), c.level);
            }
        }
    }
    TEST_CHECK(same > 0 && edge > 0 && texels > 0 && worstSame < 1e-4 && worstEdge < 1e-4 && exact);
    std::printf("sides: %zu wet columns shared by two LOD 0 tiles, %.6f m apart at most; %zu on a "
                "side stitched to LOD 1, %.6f m off its edge at most; %zu LOD 3 columns against "
                "the horizon, %s\n",
                same, worstSame, edge, worstEdge, texels,
                exact ? "the levels of its water texels" : "NOT THE LEVELS OF ITS WATER TEXELS");
}

// Whether every fall of a tile has its curtain and its foam, their sizes in uv2.v: the width of
// the curtain (kCurtainWidthShare of the fall), the radius of the foam.
bool everyFallDrawn(const TileData& t, const UnrealWaterMesh& m) {
    std::size_t foam = 0;
    bool sized = true;
    for (std::size_t n = 0; n < m.positions.size(); ++n) {
        const bool curtain = m.uv1[n].u == terrain::unreal::kWaterCurtain;
        foam += curtain ? 0u : 1u;
        bool known = false;
        for (const terrain::TileFall& f : t.falls) {
            const double width = static_cast<double>(f.width);
            const double size = curtain ? terrain::unreal::kCurtainWidthShare * width
                                        : std::max(terrain::unreal::kFoamLeastRadius,
                                                   terrain::unreal::kFoamRadiusShare * width);
            known = known || std::abs(m.uv2[n].v - size) < 1e-3;
        }
        sized = sized && known;
    }
    return sized && foam == 17u * t.falls.size();
}

// Whether every triangle faces the side of the normal of its first vertex, and the normals of the
// curtains point up (downstream).
bool facing(const UnrealWaterMesh& m) {
    bool right = true;
    for (std::size_t n = 0; n < m.positions.size(); ++n) {
        right = right && (m.uv1[n].u != terrain::unreal::kWaterCurtain || m.normals[n].z >= 0.0f);
    }
    for (std::size_t n = 0; n < m.triangleCount(); ++n) {
        const UnrealVector3f& normal = m.normals[static_cast<std::size_t>(m.triangles[3 * n])];
        const UnrealVector3f& a = m.positions[static_cast<std::size_t>(m.triangles[3 * n])];
        const UnrealVector3f& b = m.positions[static_cast<std::size_t>(m.triangles[3 * n + 1])];
        const UnrealVector3f& c = m.positions[static_cast<std::size_t>(m.triangles[3 * n + 2])];
        const double ux = c.x - a.x, uy = c.y - a.y, uz = c.z - a.z;
        const double vx = b.x - a.x, vy = b.y - a.y, vz = b.z - a.z;
        right = right && (uy * vz - uz * vy) * normal.x + (uz * vx - ux * vz) * normal.y +
                                 (ux * vy - uy * vx) * normal.z >= 0.0;
    }
    return right;
}

// Made-up falls: a steep one (54 m over 6 m), curved, from the water at its lip across
// kCurtainWidthShare of its width down to below the water at its foot, facing downstream; a
// chute (20 m over 20 m), straight from its lip to its foot.
bool madeUpCurtains() {
    TileData steep = madeUp(0, 0);
    steep.falls.push_back({4.0, 5.0, 10.0, 5.0, 150.0f, 96.5f, 20.0f});
    const UnrealWaterMesh m = terrain::unreal::buildFallCurtains(steep);
    double top = -1e9;
    double bottom = 1e9;
    for (std::size_t n = 0; n < m.positions.size(); ++n) {
        if (m.uv1[n].u == terrain::unreal::kWaterCurtain) {
            top = std::max(top, steep.originY + m.positions[n].z / 100.0);
            bottom = std::min(bottom, steep.originY + m.positions[n].z / 100.0);
        }
    }
    const double across = std::hypot(m.positions[1].x - m.positions[0].x,
                                     m.positions[1].y - m.positions[0].y);
    bool right = std::abs(top - 150.0) < 1e-3 && std::abs(bottom - 96.0) < 1e-3 &&
                 std::abs(across - 1800.0) < 1e-2 && m.normals[0].x > 0.0f && facing(m) &&
                 everyFallDrawn(steep, m);
    TileData chute = madeUp(0, 0);
    chute.falls.push_back({4.0, 5.0, 24.0, 5.0, 120.0f, 100.0f, 6.0f});
    const UnrealWaterMesh c = terrain::unreal::buildFallCurtains(chute);
    for (std::size_t n = 0; n < c.positions.size(); ++n) {
        const double y = chute.originY + c.positions[n].z / 100.0;
        if (c.uv1[n].u == terrain::unreal::kWaterCurtain && y > 100.0 - 1e-3) {
            const double ahead = c.positions[n].x / 100.0 - 4.0 - terrain::unreal::kCurtainAhead;
            right = right && std::abs(ahead / 20.0 - (120.0 - y) / 20.0) < 1e-4;
        }
    }
    return right && facing(c) && everyFallDrawn(chute, c);
}

// The curtains of the falls in the tiles of their lips at every LOD, and made-up ones: every fall
// has its curtain and its foam, facing downstream.
void testCurtains(const World& w, const std::vector<terrain::RiverFall>& falls) {
    std::size_t drawn = 0;
    bool right = madeUpCurtains();
    for (const terrain::RiverFall& f : falls) {
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            const TileData t = tileAt(w.tiles, f.x, f.z, lod);
            const UnrealWaterMesh m = terrain::unreal::buildFallCurtains(t);
            right = right && everyFallDrawn(t, m) && facing(m);
            drawn += t.falls.size();
        }
    }
    TEST_CHECK(right && drawn >= falls.size());
    std::printf("curtains: %zu falls in the tiles of their lips (LOD 0 to 3), %s\n", drawn,
                right ? "each with its curtain and its foam, sized, facing downstream"
                      : "WRONG CURTAINS");
}

// The time of the water of the tiles around the lips, every LOD, on the game thread.
void measure(const World& w, const std::vector<terrain::RiverFall>& falls) {
    std::vector<TileData> tiles;
    for (const terrain::RiverFall& f : falls) {
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            tiles.push_back(tileAt(w.tiles, f.x, f.z, lod));
        }
    }
    std::size_t triangles = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int run = 0; run < 20; ++run) {
        for (const TileData& t : tiles) {
            triangles += terrain::unreal::buildTileWater(t).triangleCount() +
                         terrain::unreal::buildFallCurtains(t).triangleCount();
        }
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("cost: %.4f ms per tile with water (%zu tiles), %zu triangles each on average\n",
                ms / (20.0 * static_cast<double>(tiles.size())), tiles.size(),
                triangles / (20u * tiles.size()));
}

std::vector<terrain::RiverFall> someFalls(const World& w, std::size_t most) {
    std::vector<terrain::RiverFall> falls;
    for (std::size_t r = 0; r < w.hydrology->reachCount() && falls.size() < most; ++r) {
        for (const terrain::RiverFall& f : w.hydrology->reach(r).falls) {
            if (falls.size() < most && f.top - f.bottom >= 4.0f) {
                falls.push_back(f);
            }
        }
    }
    return falls;
}

} // namespace

int main() {
    testLake();
    testFallAndSeam();
    const World w;
    const std::vector<terrain::RiverFall> falls = someFalls(w, 8);
    testSides(w, falls);
    testCurtains(w, falls);
    measure(w, falls);
    return test::result();
}
