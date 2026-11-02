// The edits of the players kept between two plays of a level (agent 28): a break and a place
// aimed at the ground of a session, written by the save thread into the folder of the seed (world
// format v2), read back by another session, whose tile is then the same to the bit; a folder
// without a world, another seed and a damaged file refused with a clear reason, never a crash.
#include "edit_save.hpp"

#include "block_targeting.hpp"
#include "terrain_session.hpp"
#include "test_assert.hpp"

#include "terrain/storage/world_save.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using terrain::ChunkCoord;
using terrain::TileKey;
using terrain::unreal::BuildAction;
using terrain::unreal::BuildTool;
using terrain::unreal::EditSaver;
using terrain::unreal::SessionParams;
using terrain::unreal::TerrainSession;
using terrain::unreal::TileUpload;
using terrain::unreal::UnrealFrame;

constexpr std::uint64_t kSeed = 20261102;

fs::path freshRoot(const char* name) {
    const fs::path root = fs::temp_directory_path() / "terrain_edit_save_test" / name;
    std::error_code ignored;
    fs::remove_all(root, ignored);
    return root;
}

SessionParams smallSession() {
    SessionParams params;
    params.seed = kSeed;
    params.stream.lod = {{2, 4, 8}, 6};
    params.stream.workerThreads = 2;
    params.stream.maxLoadsPerUpdate = 16;
    return params;
}

// The edits of a player at the ground of (x, z): a sphere of 4 m broken, a block placed beside.
void editAt(TerrainSession& session, double x, double z) {
    const double y = session.tiles().heightfield().heightAt(x, z);
    const terrain::unreal::BuildHit hit{{x, y, z}, {0.0, 1.0, 0.0}, 3.0};
    session.queueEdit(buildTarget(hit, BuildTool::Sphere4, BuildAction::Break).edit, "break");
    const terrain::unreal::BuildHit beside{{x + 6.0, y, z}, {0.0, 1.0, 0.0}, 3.0};
    session.queueEdit(buildTarget(beside, BuildTool::Block, BuildAction::Place).edit, "place");
    session.waitEdits();
}

std::optional<TileUpload> groundTile(TerrainSession& session, const UnrealFrame& frame) {
    const terrain::unreal::WarmUpResult warm = session.streamer().warmUp(
        terrain::unreal::toUnreal(frame, {8.0, 0.0, 8.0}), frame, 1, std::chrono::seconds(60));
    std::optional<TileUpload> found;
    for (const auto& update : warm.updates) {
        for (const TileUpload& upload : update.uploads) {
            if (upload.key == TileKey{ChunkCoord{0, 0}, 0}) {
                found = upload;
            }
        }
    }
    return found;
}

void testRoundTrip() {
    const fs::path folder = terrain::unreal::editSaveFolder(freshRoot("round_trip"), kSeed);
    TEST_CHECK(folder.filename() == "seed_20261102");
    TEST_CHECK(!terrain::unreal::loadEdits(folder, kSeed, terrain::WorldSize{}).found);
    std::optional<TileUpload> edited;
    terrain::ModificationStore saved;
    {
        TerrainSession session(smallSession());
        editAt(session, 8.0, 8.0);
        const std::vector<terrain::unreal::EditOutcome> done = session.takeEdits();
        TEST_CHECK(done.size() == 2);
        EditSaver saver(folder, terrain::unreal::editWorldMeta(kSeed, terrain::WorldSize{}),
                        [&session] { return session.tiles().modifications(); });
        TEST_CHECK(!saver.save()); // nothing marked yet
        for (const auto& outcome : done) {
            TEST_CHECK(outcome.error == nullptr);
            saver.markEdited(outcome.chunks);
        }
        TEST_CHECK(saver.dirtyRegions() == 1);
        TEST_CHECK(saver.save());
        saver.flush();
        TEST_CHECK(saver.report().saves == 1 && saver.report().lastError.empty());
        edited = groundTile(session, session.frameAt(0.0, 0.0));
        saved = session.tiles().modifications();
    }
    TEST_CHECK(saved.blockCount() > 0);
    const terrain::unreal::EditLoad load =
        terrain::unreal::loadEdits(folder, kSeed, terrain::WorldSize{});
    TEST_CHECK(load.found && load.error.empty());
    TEST_CHECK(load.modifications == saved);
    TerrainSession again(smallSession());
    (void)again.replaceModifications(load.modifications);
    const std::optional<TileUpload> reloaded = groundTile(again, again.frameAt(0.0, 0.0));
    TEST_CHECK(edited && reloaded && edited->mesh == reloaded->mesh);
    TEST_CHECK(edited && reloaded && edited->collision && reloaded->collision);
}

void testRefusals() {
    const fs::path root = freshRoot("refusals");
    const fs::path folder = terrain::unreal::editSaveFolder(root, kSeed);
    terrain::ModificationStore store;
    store.setBlock({1, 2, 3}, terrain::BlockEdit::Removed);
    terrain::saveWorld(folder, terrain::unreal::editWorldMeta(kSeed, terrain::WorldSize{}), store);
    // Another seed, another size of world.
    const auto other = terrain::unreal::loadEdits(folder, kSeed + 1, terrain::WorldSize{});
    TEST_CHECK(other.found && !other.error.empty() && other.modifications.empty());
    TEST_CHECK(other.error.find("seed 20261102") != std::string::npos);
    terrain::WorldSize small;
    small.x = 65536.0 * 4;
    TEST_CHECK(!terrain::unreal::loadEdits(folder, kSeed, small).error.empty());
    // world.meta damaged and its .bak gone: refused with the reason of the storage.
    fs::remove(folder / "world.meta.bak");
    {
        std::ofstream damaged(folder / "world.meta", std::ios::binary | std::ios::trunc);
        damaged << "not a world";
    }
    const auto broken = terrain::unreal::loadEdits(folder, kSeed, terrain::WorldSize{});
    TEST_CHECK(broken.found && !broken.error.empty() && broken.modifications.empty());
    TEST_CHECK(broken.error.find("are not loaded") != std::string::npos);
}

void testFailedSaveKept() {
    // A folder that is a file: the save fails, its regions stay marked for the next save.
    const fs::path root = freshRoot("failed");
    fs::create_directories(root);
    const fs::path folder = root / "seed_1";
    std::ofstream(folder) << "a file";
    terrain::ModificationStore store;
    store.setBlock({1, 2, 3}, terrain::BlockEdit::Added);
    EditSaver saver(folder, terrain::unreal::editWorldMeta(1, terrain::WorldSize{}),
                    [store] { return store; });
    const std::vector<ChunkCoord> chunks = {ChunkCoord{0, 0}};
    saver.markEdited(chunks);
    TEST_CHECK(saver.save());
    saver.flush();
    TEST_CHECK(!saver.report().lastError.empty() && saver.report().saves == 0);
    TEST_CHECK(saver.dirtyRegions() == 1);
}

} // namespace

int main() {
    testRoundTrip();
    testRefusals();
    testFailedSaveKept();
    return test::result();
}
