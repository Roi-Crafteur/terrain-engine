// The edits of the players through the streaming of the Unreal plugin (agent 41, the second test
// of #78 in Unreal): a session of the real world (the size of the planet, far from the origin, at
// the negative coordinates of the scenes of BREAK_PLACE.md), its edit thread as a click uses it,
// its streamer and the pacer of the uploads into meshes kept as the backend keeps them. After the
// click, after moves of 4, 8 and 16 m, after a teleport that unloads every tile and the return,
// and with the saved edits read before or after the first load, every tile shown around the edit
// is the tile the core generates now (no tile left without the edit), and the LOD 0 tiles over
// the edit are dug or raised there. A sphere across two and four tiles: every tile dug on its
// side, the shared sides within 0.5 mm, at LOD 0, 1 and 2. The ground of a column read from the
// triangles (tile_column.hpp) on known meshes. With --planet <file>, the same with the water of
// the world (a local run: 26 s).
#include "block_targeting.hpp"
#include "tile_column.hpp"
#include "unreal_edit_level.hpp"

#include "test_assert.hpp"

#include "terrain/storage/volume_edit.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::ChunkCoord;
using terrain::TileKey;
using terrain::VolumeEdit;
using terrain::unreal::CorePoint;
using terrain::unreal::TileUpload;

// The planet file of a local run (argument --planet <file>): the tiles with the water of the
// world, as Unreal plays it. None in CTest: the relief of the world of the planet alone.
std::string gPlanetFile;

// A level of the world of the planet (unreal_edit_level.hpp), with the planet file if given.
struct Level : edit_level::Level {
    Level(int workers, double x, double z) : edit_level::Level(workers, x, z, gPlanetFile) {}
};

// What a click at the point of the ground at (x, z), or at a point given, asks for: the hit
// with a normal up, as Terrain.EditAim ... at x y z gives it to the code of the click.
VolumeEdit aimed(const terrain::unreal::CorePoint& point, terrain::unreal::BuildTool tool,
                 terrain::unreal::BuildAction action, double radius = 39.0 / 16.0) {
    const terrain::unreal::BuildHit hit{point, {0.0, 1.0, 0.0}, 2.0};
    return terrain::unreal::buildTarget(hit, tool, action, radius).edit;
}

CorePoint groundPoint(const Level& level, double x, double z) {
    return {x, level.session.tiles().groundAt(x, z), z};
}

VolumeEdit dig(const Level& level, double x, double z) {
    return aimed(groundPoint(level, x, z), terrain::unreal::BuildTool::Block,
                 terrain::unreal::BuildAction::Break);
}

VolumeEdit digSphere(const Level& level, double x, double z) {
    return aimed(groundPoint(level, x, z), terrain::unreal::BuildTool::Sphere4,
                 terrain::unreal::BuildAction::Break);
}

VolumeEdit place(const CorePoint& point) {
    return aimed(point, terrain::unreal::BuildTool::Block, terrain::unreal::BuildAction::Place);
}

bool overlaps(const TileKey& key, double x0, double x1, double z0, double z1) {
    const double side = terrain::kChunkSizeMeters * terrain::tileChunks(key.lod);
    const double ox = key.origin.x * terrain::kChunkSizeMeters;
    const double oz = key.origin.z * terrain::kChunkSizeMeters;
    return ox <= x1 && ox + side >= x0 && oz <= z1 && oz + side >= z0;
}

// The shown tiles over [x0, x1] x [z0, z1] whose mesh is not the one the core generates now for
// their request in the selection: tiles left without an edit, or with an old one.
int staleTiles(Level& level, double x0, double x1, double z0, double z1,
               const char* step) {
    int stale = 0;
    for (const terrain::TileRequest& request : level.session.streamer().manager().selection()) {
        if (!overlaps(request.key, x0, x1, z0, z1)) {
            continue;
        }
        const auto shown = level.meshes.tiles.find(request.key);
        if (shown == level.meshes.tiles.end()) {
            std::printf("  %s: tile (%d, %d) LOD %d not shown\n", step, request.key.origin.x,
                        request.key.origin.z, request.key.lod);
            ++stale;
            continue;
        }
        const terrain::TileData now = level.session.tiles().generate(request);
        const std::vector<terrain::unreal::UnrealVector3f>& positions =
            shown->second.mesh.positions;
        bool same = positions.size() == now.mesh.vertices.size();
        for (std::size_t i = 0; same && i < positions.size(); ++i) {
            same = positions[i] == terrain::unreal::toUnrealOffset(now.mesh.vertices[i]);
        }
        if (!same) {
            std::printf("  %s: tile (%d, %d) LOD %d shows %zu vertices, the core %zu now\n", step,
                        request.key.origin.x, request.key.origin.z, request.key.lod,
                        positions.size(), now.mesh.vertices.size());
            ++stale;
        }
    }
    return stale;
}

// The ground the shown LOD 0 tiles put on the vertical line through (x, z) (tile_column.hpp),
// -infinity where none crosses it.
double shownTopAt(const Level& level, double x, double z) {
    double top = -std::numeric_limits<double>::infinity();
    for (const auto& [key, tile] : level.meshes.tiles) {
        if (key.lod != 0) {
            continue;
        }
        const std::vector<CorePoint> vertices = terrain::unreal::coreVertices(level.frame, tile);
        const std::optional<double> here =
            terrain::unreal::surfaceTop(vertices, tile.mesh.triangles, x, z);
        top = here ? std::max(top, *here) : top;
    }
    return top;
}

// The column of a block: the vertical line through its lattice point.
double shownTop(const Level& level, int bx, int bz) {
    return shownTopAt(level, bx, bz);
}

// A removed block (the cube of side 1 m centered on its lattice point) with ground under it: the
// shown floor of its column at its bottom face.
bool dug(const Level& level, BlockCoord b) {
    return shownTop(level, b.x, b.z) < b.y - 0.49;
}

// An added block: the shown top of its column at its top face.
bool raised(const Level& level, BlockCoord b) {
    return shownTop(level, b.x, b.z) > b.y + 0.49;
}

} // namespace

namespace {

constexpr double kAx = -240628.0; // scene a: the sphere
constexpr double kAz = -86717.0;
constexpr double kCx = -240628.0; // scene c: the hole, and the wall from z -86671 to -86664
constexpr double kCz = -86677.0;

// Every shown tile over the edits of scenes a and c is the tile of the core now, and the LOD 0
// tiles show the sphere, the hole and both rows of the wall.
void checkScenes(Level& level, const BlockCoord& sphereBottom, const BlockCoord& hole,
                 const BlockCoord& top, const char* step) {
    TEST_CHECK(staleTiles(level, kAx - 16.0, kAx + 16.0, kAz - 16.0, kCz + 16.0, step) == 0);
    TEST_CHECK(dug(level, sphereBottom));
    TEST_CHECK(dug(level, hole));
    TEST_CHECK(raised(level, top));
    if (!dug(level, sphereBottom) || !dug(level, hole) || !raised(level, top)) {
        std::printf("  %s: tops %.3f (sphere) %.3f (hole) %.3f (wall)\n", step,
                    shownTop(level, sphereBottom.x, sphereBottom.z),
                    shownTop(level, hole.x, hole.z),
                    shownTop(level, top.x, top.z));
    }
}

struct SceneEdits {
    BlockCoord sphereBottom;
    BlockCoord hole;
    BlockCoord top;
};

// The clicks of scenes a and c (BREAK_PLACE.md), each from where Terrain.Goto puts the pawn.
SceneEdits clickScenes(Level& level) {
    level.play(-240632.0, kAz); // Terrain.Goto modif_sphere, then Terrain.Pawn
    level.warmUp(-240632.0, kAz);
    const VolumeEdit sphere = digSphere(level, kAx, kAz);
    level.click(sphere, -240632.0, kAz);
    level.play(-240631.0, kCz); // plaine_casser
    level.warmUp(-240631.0, kCz);
    const VolumeEdit hole = dig(level, kCx, kCz);
    level.click(hole, -240631.0, kCz);
    level.play(-240631.0, -86668.0); // modif_mur
    level.warmUp(-240631.0, -86668.0);
    BlockCoord top{};
    for (int z = -86671; z <= -86664; ++z) {
        const VolumeEdit first = place(groundPoint(level, kCx, z));
        level.click(first, -240631.0, -86668.0);
        // The second row on the top face of the first, at its center.
        const VolumeEdit second = place({kCx, first.anchor.y + 0.5, static_cast<double>(z)});
        TEST_CHECK(second.anchor.y == first.anchor.y + 1);
        level.click(second, -240631.0, -86668.0);
        top = second.anchor;
    }
    const BlockCoord bottom{sphere.anchor.x, sphere.anchor.y - 1, sphere.anchor.z};
    return {bottom, hole.anchor, top};
}

// Scenes a and c, looked at after moves of 4, 8 and 16 m (Terrain.Goto nearby), then after a
// teleport that unloads every tile and the way back.
void testClicksThenMoves(int workers) {
    Level level(workers, -240632.0, kAz);
    const SceneEdits edits = clickScenes(level);
    checkScenes(level, edits.sphereBottom, edits.hole, edits.top, "clicked");
    for (const double away : {4.0, 8.0, 16.0, 40.0}) {
        level.play(-240636.0 - away, -86690.0);
        const std::string step = "moved " + std::to_string(away);
        checkScenes(level, edits.sphereBottom, edits.hole, edits.top, step.c_str());
    }
    level.play(-238000.0, -86670.0);
    TEST_CHECK(level.meshes.tiles.count(TileKey{{-15040, -5418}, 0}) == 0);
    level.play(-240638.0, -86670.0); // modif_vue_trou_mur
    checkScenes(level, edits.sphereBottom, edits.hole, edits.top, "back");
}

// The edits saved (the store of the tiles) read by a new level, as Terrain.EditRead 1 and
// Building.bSaveEdits read them (LoadSaved: replaceModifications): before its first load, or
// after it; then a teleport and the way back.
void testSavedEditsRead() {
    Level first(0, -240632.0, kAz);
    const SceneEdits edits = clickScenes(first);
    const terrain::ModificationStore saved = first.session.tiles().modifications();
    Level before(0, -240638.0, -86670.0);
    static_cast<void>(before.session.replaceModifications(saved));
    before.play(-240638.0, -86670.0);
    checkScenes(before, edits.sphereBottom, edits.hole, edits.top, "read before the load");
    Level after(0, -240638.0, -86670.0);
    after.play(-240638.0, -86670.0);
    static_cast<void>(after.session.replaceModifications(saved));
    after.play(-240638.0, -86670.0);
    checkScenes(after, edits.sphereBottom, edits.hole, edits.top, "read after the load");
    after.play(-237000.0, -86670.0);
    after.play(-240636.0, kAz); // modif_vue_sphere
    checkScenes(after, edits.sphereBottom, edits.hole, edits.top, "read, back");
}

} // namespace

namespace {

// The widest gap between the vertices two shown tiles of a LOD put on the plane they share
// (x = at along x, z = at along z) within [from, to] of the other axis: for each vertex of one,
// the nearest of the other; -1 without vertices on both sides.
double seamGap(const Level& level, int lod, bool alongX, double at, double from, double to) {
    std::vector<CorePoint> low;
    std::vector<CorePoint> high;
    for (const auto& [key, tile] : level.meshes.tiles) {
        if (key.lod != lod) {
            continue;
        }
        const double side = terrain::kChunkSizeMeters * terrain::tileChunks(lod);
        const double origin = (alongX ? key.origin.x : key.origin.z) * terrain::kChunkSizeMeters;
        std::vector<CorePoint>* const points =
            origin + side == at ? &low : (origin == at ? &high : nullptr);
        if (points == nullptr) {
            continue;
        }
        for (const CorePoint& c : terrain::unreal::coreVertices(level.frame, tile)) {
            const double across = alongX ? c.x : c.z;
            const double along = alongX ? c.z : c.x;
            if (std::abs(across - at) < 1e-3 && along >= from && along <= to) {
                points->push_back(c);
            }
        }
    }
    if (low.empty() || high.empty()) {
        return -1.0;
    }
    double widest = 0.0;
    for (const std::vector<CorePoint>* one : {&low, &high}) {
        const std::vector<CorePoint>& other = one == &low ? high : low;
        for (const CorePoint& a : *one) {
            double nearest = std::numeric_limits<double>::infinity();
            for (const CorePoint& b : other) {
                nearest = std::min(nearest, std::hypot(a.x - b.x, a.y - b.y, a.z - b.z));
            }
            widest = std::max(widest, nearest);
        }
    }
    return widest;
}

// Dug well below the natural ground at (x, z): inside a hollow, not on the ground beside it.
bool deepAt(const Level& level, double x, double z) {
    return shownTopAt(level, x, z) < level.session.tiles().groundAt(x, z) - 1.0;
}

// A sphere of 39/16 m across two tiles (scene b, the chunk side at x -240608) and one across four
// (its center on the corner of four chunks): each tile dug on its side, the shared sides within
// 0.5 mm; then the same from farther, its tiles at LOD 1 and at LOD 2.
void testSphereAcrossTiles() {
    constexpr double kBx = -240610.0; // scene b: chunks -15039 and -15038 along x
    constexpr double kBz = -86697.0;
    constexpr double kCornerX = -240608.0; // the corner of chunks (-15039..-15038, -5420..-5419)
    constexpr double kCornerZ = -86704.0;
    Level level(0, -240614.0, kBz);
    level.play(-240614.0, kBz); // modif_tunnel
    level.click(digSphere(level, kBx, kBz), -240614.0, kBz);
    TEST_CHECK(deepAt(level, -240609.0, kBz));
    TEST_CHECK(deepAt(level, -240607.75, kBz)); // in the tile beyond x -240608
    const double twoGap = seamGap(level, 0, true, -240608.0, kBz - 3.0, kBz + 3.0);
    std::printf("sphere across two tiles: the seam within %.6f m\n", twoGap);
    TEST_CHECK(twoGap >= 0.0 && twoGap <= 0.0005);
    level.click(digSphere(level, kCornerX, kCornerZ), -240614.0, kBz);
    for (const double dx : {-0.25, 0.25}) {
        for (const double dz : {-0.25, 0.25}) {
            TEST_CHECK(deepAt(level, kCornerX + dx, kCornerZ + dz));
        }
    }
    const double cornerX = seamGap(level, 0, true, kCornerX, kCornerZ - 3.0, kCornerZ + 3.0);
    const double cornerZ = seamGap(level, 0, false, kCornerZ, kCornerX - 3.0, kCornerX + 3.0);
    std::printf("sphere across four tiles: the seams within %.6f and %.6f m\n", cornerX, cornerZ);
    TEST_CHECK(cornerX >= 0.0 && cornerX <= 0.0005);
    TEST_CHECK(cornerZ >= 0.0 && cornerZ <= 0.0005);
    TEST_CHECK(staleTiles(level, kBx - 8.0, kBx + 8.0, kCornerZ - 8.0, kBz + 8.0, "lod 0") == 0);
    level.play(kBx, kBz - 90.0); // the tiles of the spheres at LOD 1
    TEST_CHECK(staleTiles(level, kBx - 8.0, kBx + 8.0, kCornerZ - 8.0, kBz + 8.0, "lod 1") == 0);
    const double lod1 = seamGap(level, 1, true, -240608.0, kCornerZ - 3.0, kBz + 3.0);
    std::printf("at LOD 1: the seam within %.6f m\n", lod1);
    TEST_CHECK(lod1 >= 0.0 && lod1 <= 0.0005);
    level.play(kBx, kBz - 170.0); // at LOD 2
    TEST_CHECK(staleTiles(level, kBx - 8.0, kBx + 8.0, kCornerZ - 8.0, kBz + 8.0, "lod 2") == 0);
}

} // namespace

namespace {

// surfaceTop on known meshes: a flat square at 2 m (two triangles), the same square around a hole
// of 1 m (eight triangles), a point on the shared edge, a point outside, two levels over a column.
void testSurfaceTop() {
    using terrain::unreal::surfaceTop;
    const std::vector<CorePoint> square = {{0, 2, 0}, {4, 2, 0}, {4, 2, 4}, {0, 2, 4}};
    const std::vector<std::uint32_t> two = {0, 1, 2, 0, 2, 3};
    TEST_CHECK(surfaceTop(square, two, 1.0, 3.0) == 2.0);
    TEST_CHECK(surfaceTop(square, two, 2.0, 2.0) == 2.0); // on the diagonal both share
    TEST_CHECK(!surfaceTop(square, two, 5.0, 2.0));
    // A frame around the hole [1.5, 2.5]^2: the outer corners 0-3, the inner 4-7.
    const std::vector<CorePoint> frame = {{0, 2, 0},     {4, 2, 0},     {4, 2, 4},
                                          {0, 2, 4},     {1.5, 2, 1.5}, {2.5, 2, 1.5},
                                          {2.5, 2, 2.5}, {1.5, 2, 2.5}};
    const std::vector<std::int32_t> ring = {0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5,
                                            2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7};
    TEST_CHECK(!surfaceTop(frame, ring, 2.0, 2.0));
    TEST_CHECK(surfaceTop(frame, ring, 0.5, 2.0) == 2.0);
    // A floor at -3 m under the hole: the top is the floor there, the frame elsewhere.
    std::vector<CorePoint> both = frame;
    both.insert(both.end(), {{1.5, -3, 1.5}, {2.5, -3, 1.5}, {2.5, -3, 2.5}, {1.5, -3, 2.5}});
    std::vector<std::int32_t> withFloor = ring;
    withFloor.insert(withFloor.end(), {8, 9, 10, 8, 10, 11});
    TEST_CHECK(surfaceTop(both, withFloor, 2.0, 2.0) == -3.0);
    TEST_CHECK(surfaceTop(both, withFloor, 0.5, 0.5) == 2.0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--planet") {
        gPlanetFile = argv[2];
    }
    testSurfaceTop();
    testClicksThenMoves(0);
    testClicksThenMoves(2);
    testSavedEditsRead();
    testSphereAcrossTiles();
    return test::result();
}
