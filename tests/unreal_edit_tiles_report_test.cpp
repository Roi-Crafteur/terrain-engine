// Terrain.EditTiles in the bridge (agent 41, Bridge/edit_tiles_report.hpp): after a sphere dug
// across the side of two tiles, the tiles over a point on that side are both LOD 0 tiles of the
// streamer; for each, the ground of the column in the core and in the mesh the backend received
// agree (the hollow), the LOD from the camera is the LOD of the tile, the point lies in the
// bounds; a mesh of before the click, a hidden mesh, bounds that miss the point and a tile without
// a mesh are told so. Over a tunnel, the ground, its ceiling and its floor. The LOD a player at
// the camera asks for, and the line of the positions.
#include "edit_tiles_report.hpp"

#include "block_targeting.hpp"
#include "unreal_edit_level.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::TileKey;
using terrain::unreal::ShownSection;
using terrain::unreal::TileColumnReport;
using terrain::unreal::TileUpload;
using terrain::unreal::UnrealPoint;

// What the procedural mesh of the backend holds after SetTile: the vertices placed in the world,
// the triangles, bounds around them.
ShownSection sectionOf(const TileUpload& upload) {
    ShownSection section;
    UnrealPoint low{1e300, 1e300, 1e300};
    UnrealPoint high{-1e300, -1e300, -1e300};
    for (const terrain::unreal::UnrealVector3f& p : upload.mesh.positions) {
        const UnrealPoint at{upload.origin.x + p.x, upload.origin.y + p.y, upload.origin.z + p.z};
        section.positions.push_back(at);
        low = {std::min(low.x, at.x), std::min(low.y, at.y), std::min(low.z, at.z)};
        high = {std::max(high.x, at.x), std::max(high.y, at.y), std::max(high.z, at.z)};
    }
    for (const std::int32_t index : upload.mesh.triangles) {
        section.triangles.push_back(static_cast<std::uint32_t>(index));
    }
    section.boundsMin = low;
    section.boundsMax = high;
    section.visible = true;
    section.sectionVisible = true;
    section.uploadedSecondsAgo = 1.5;
    section.uploads = 2;
    return section;
}

bool has(const std::string& line, const char* part) {
    return line.find(part) != std::string::npos;
}

// The sphere of scene b (BREAK_PLACE.md) across the side x -240608 of two LOD 0 tiles.
void testTilesOverAHollow() {
    constexpr double kPlayerX = -240614.0;
    constexpr double kZ = -86697.0;
    edit_level::Level level(0, kPlayerX, kZ);
    level.play(kPlayerX, kZ);
    const double ground = level.session.tiles().groundAt(-240610.0, kZ);
    const TileKey west{{-15039, -5419}, 0};
    const TileUpload before = level.meshes.tiles.at(west);
    const terrain::unreal::BuildHit hit{{-240610.0, ground, kZ}, {0.0, 1.0, 0.0}, 2.0};
    level.click(terrain::unreal::buildTarget(hit, terrain::unreal::BuildTool::Sphere4,
                                             terrain::unreal::BuildAction::Break)
                    .edit,
                kPlayerX, kZ);
    const ChunkCoord camera = terrain::worldToChunk(kPlayerX, kZ);
    const std::vector<TileKey> keys = terrain::unreal::tilesOver(level.session.streamer(),
                                                                 -240608.0, kZ);
    TEST_CHECK(keys.size() == 2);
    for (const TileKey& key : keys) {
        TEST_CHECK(key.lod == 0);
        const TileColumnReport report = terrain::unreal::reportTile(
            level.session.streamer(), level.frame, key, -240608.0, kZ, camera,
            sectionOf(level.meshes.tiles.at(key)));
        TEST_CHECK(report.inStreamer && report.inMesh && report.coreTop && report.shownTop);
        TEST_CHECK(report.coreTop && *report.coreTop < ground - 1.0);
        TEST_CHECK(report.coreTop && report.shownTop &&
                   std::abs(*report.coreTop - *report.shownTop) < 0.01);
        TEST_CHECK(report.expectedLod == 0 && report.editedVertices > 0 && report.pointInBounds);
        const std::string line = terrain::unreal::editTilesLine(-240608.0, kZ, report);
        std::printf("%s\n", line.c_str());
        TEST_CHECK(has(line, "LOD 0 (from the camera: LOD 0)") && has(line, ", visible"));
        TEST_CHECK(!has(line, "DIFFERENT") && has(line, "uploaded 1.5 s ago, upload 2"));
    }
    // The mesh of before the click, hidden, its bounds away from the point; then no mesh at all.
    ShownSection old = sectionOf(before);
    old.visible = false;
    old.boundsMax = old.boundsMin;
    const TileColumnReport stale = terrain::unreal::reportTile(
        level.session.streamer(), level.frame, west, -240609.0, kZ, camera, old);
    const std::string staleLine = terrain::unreal::editTilesLine(-240609.0, kZ, stale);
    std::printf("%s\n", staleLine.c_str());
    TEST_CHECK(stale.shownTop && std::abs(*stale.shownTop - ground) < 0.5);
    TEST_CHECK(has(staleLine, "DIFFERENT from the core") && has(staleLine, "HIDDEN") &&
               has(staleLine, "the point OUTSIDE"));
    const TileColumnReport none = terrain::unreal::reportTile(
        level.session.streamer(), level.frame, west, -240609.0, kZ, camera, std::nullopt);
    TEST_CHECK(has(terrain::unreal::editTilesLine(-240609.0, kZ, none), "| no mesh in Unreal"));
}

// A tunnel of two rows under the ground (scene b): over it, the ground, the ceiling and the floor
// of the tunnel, the same in the core and in the mesh.
void testTilesOverATunnel() {
    constexpr double kX = -240614.0;
    constexpr double kZ = -86697.0;
    edit_level::Level level(0, kX, kZ);
    level.play(kX, kZ);
    // Two rows with a meter of rock at least over them.
    const int floorRow =
        static_cast<int>(std::floor(level.session.tiles().groundAt(-240607.0, kZ))) - 3;
    for (const int y : {floorRow, floorRow + 1}) {
        terrain::VolumeEdit block;
        block.shape = terrain::EditShape::Box;
        block.mode = terrain::EditMode::Remove;
        block.anchor = {-240607, y, -86697};
        block.size = {1, 1, 1};
        level.click(block, kX, kZ);
    }
    const TileKey key{{-15038, -5419}, 0};
    const TileColumnReport report = terrain::unreal::reportTile(
        level.session.streamer(), level.frame, key, -240607.0, kZ,
        terrain::worldToChunk(kX, kZ), sectionOf(level.meshes.tiles.at(key)));
    const std::string line = terrain::unreal::editTilesLine(-240607.0, kZ, report);
    std::printf("%s\n", line.c_str());
    TEST_CHECK(report.coreHeights.size() == 3 && report.shownHeights.size() == 3);
    if (report.coreHeights.size() == 3) {
        TEST_CHECK(std::abs(report.coreHeights[1] - (floorRow + 1.5)) < 1e-4); // the ceiling
        TEST_CHECK(std::abs(report.coreHeights[2] - (floorRow - 0.5)) < 1e-4); // the floor
    }
    TEST_CHECK(has(line, ", under it ") && !has(line, "DIFFERENT"));
}

// The LOD a player at the camera asks for over a point: the rings of Terrain World.
void testExpectedLod() {
    const terrain::LodParams rings{{4, 8, 16}, 32};
    TEST_CHECK(terrain::unreal::expectedLodAt(rings, {0, 0}, 8.0, 8.0) == 0);
    TEST_CHECK(terrain::unreal::expectedLodAt(rings, {0, 0}, -50.0, 60.0) == 0);
    TEST_CHECK(terrain::unreal::expectedLodAt(rings, {0, 0}, 6.5 * 16.0, 8.0) == 1);
    TEST_CHECK(terrain::unreal::expectedLodAt(rings, {0, 0}, 12.5 * 16.0, 8.0) == 2);
    TEST_CHECK(terrain::unreal::expectedLodAt(rings, {0, 0}, 24.5 * 16.0, 8.0) == 3);
    TEST_CHECK(terrain::unreal::expectedLodAt(rings, {0, 0}, 60.5 * 16.0, 8.0) == -1);
}

// The first line: the point streamed around, the camera and the pawn, their chunks and distances.
void testPositionsLine() {
    const terrain::unreal::UnrealFrame frame{{-240600.0, 6.0, -86700.0}};
    terrain::unreal::ViewPositions view;
    view.focus = {0.0, 0.0, 0.0};
    view.camera = {300.0, 400.0, 0.0}; // 3 m east, 4 m south: 5 m away
    view.pawn = UnrealPoint{300.0, 400.0, 1200.0};
    view.pawnClass = "SpectatorPawn";
    const std::string line = terrain::unreal::positionsLine(frame, -240597.0, -86696.0, view);
    std::printf("%s\n", line.c_str());
    TEST_CHECK(has(line, "EditTiles (-240597.00, -86696.00): streaming (the pawn possessed) at "
                         "(-240600.00, 6.00, -86700.00) chunk (-15038, -5419)"));
    TEST_CHECK(has(line, "camera at (-240597.00, 6.00, -86696.00) chunk (-15038, -5419) 5.0 m "
                         "from it"));
    TEST_CHECK(has(line, "pawn SpectatorPawn at (-240597.00, 18.00, -86696.00)") &&
               has(line, "12.0 m from the camera"));
    view.pawn.reset();
    view.focusActor = true;
    const std::string alone = terrain::unreal::positionsLine(frame, 0.0, 0.0, view);
    TEST_CHECK(has(alone, "streaming (FocusActor)") && has(alone, ", no pawn"));
}

} // namespace

int main() {
    testExpectedLod();
    testPositionsLine();
    testTilesOverAHollow();
    testTilesOverATunnel();
    return test::result();
}
