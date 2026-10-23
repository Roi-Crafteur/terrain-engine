// World folders: a save and its reload give the same files byte for byte, the incremental save
// touches only its regions, abandoned temporary files and failed saves never damage a world, a
// damaged or missing file is read from its .bak copy and reported, the errors are clear, unknown
// files and reserved directories are left alone, a file held open by another reader never
// damages the world, a changed generator is reported, a world of format 1 is read and saved in
// the current format, and a large destruction keeps its previous version.
#include "terrain/storage/world_save.hpp"

#include "storage_test_support.hpp"
#include "test_assert.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using storage_test::errorOf;
using storage_test::fileBytes;
using storage_test::kSeed;
using storage_test::writeBytes;
using terrain::BlockEdit;
using terrain::LoadedWorld;
using terrain::ModificationStore;
using terrain::RegionCoord;
using terrain::StorageErrorKind;
using terrain::WorldMeta;

fs::path regionFile(const fs::path& world, RegionCoord region) {
    return world / terrain::regionRelativePath(region);
}

fs::path suffixed(fs::path file, const char* suffix) {
    file += suffix;
    return file;
}

std::string relative(RegionCoord region, const char* suffix = "") {
    return terrain::regionRelativePath(region).generic_string() + suffix;
}

std::optional<StorageErrorKind> loadError(const fs::path& world, std::string* message = nullptr) {
    return errorOf([&world] { static_cast<void>(terrain::loadWorld(world)); }, message);
}

void testRoundTrip(const fs::path& dir) {
    const fs::path first = dir / "first";
    const WorldMeta meta = storage_test::sampleMeta();
    const ModificationStore store = storage_test::sampleStore();
    terrain::saveWorld(first, meta, store);
    const auto files = storage_test::filesUnder(first);
    std::set<std::string> expected = {"world.meta"};
    for (const RegionCoord region : terrain::regionsOf(store)) {
        expected.insert(relative(region));
        TEST_CHECK(files.at(relative(region)) == terrain::serializeRegion(store, region, kSeed));
    }
    std::set<std::string> names;
    for (const auto& [name, bytes] : files) {
        names.insert(name);
    }
    TEST_CHECK(names == expected);
    TEST_CHECK(files.at("world.meta") == terrain::serializeWorldMeta(meta));
    TEST_CHECK(terrain::savedRegions(first) == terrain::regionsOf(store));

    const LoadedWorld loaded = terrain::loadWorld(first);
    TEST_CHECK(loaded.meta == meta && loaded.modifications == store);
    TEST_CHECK(!loaded.generatorChanged && loaded.restored.empty() && loaded.warnings.empty());
    terrain::saveWorld(dir / "second", loaded.meta, loaded.modifications);
    TEST_CHECK(storage_test::filesUnder(dir / "second") == files);

    const terrain::LoadedRegion region = terrain::loadRegion(first, {0, -1}, kSeed);
    TEST_CHECK(!region.restored && region.modifications.byChunk().size() == 2);
    TEST_CHECK(*region.modifications.chunk({1, -1}) == *store.chunk({1, -1}));
    TEST_CHECK(terrain::loadRegion(first, {7, 7}, kSeed).modifications.empty());

    terrain::saveWorld(dir / "empty", meta, {});
    TEST_CHECK(storage_test::filesUnder(dir / "empty").size() == 1);
    TEST_CHECK(terrain::loadWorld(dir / "empty").modifications.empty());
}

void testIncrementalSave(const fs::path& dir) {
    const fs::path world = dir / "incremental";
    const WorldMeta meta = storage_test::sampleMeta();
    const ModificationStore store = storage_test::sampleStore();
    terrain::saveWorld(world, meta, store);
    const auto before = storage_test::filesUnder(world);

    ModificationStore edited = store;
    edited.clearBlock({600, 5, 600});                  // region (1, 1) left without modifications
    edited.setBlock({4, 41, -2}, BlockEdit::Removed);  // region (0, -1)
    WorldMeta later = meta;
    later.savedAt += 60;
    later.playTimeSeconds += 60;
    const std::vector<RegionCoord> edits = {{1, 1}, {0, -1}, {1, 1}};
    terrain::saveWorldRegions(world, later, edited, edits);
    const auto after = storage_test::filesUnder(world);
    TEST_CHECK(!after.contains(relative({1, 1})) && !after.contains(relative({1, 1}, ".bak")));
    TEST_CHECK(after.at(relative({0, -1})) == terrain::serializeRegion(edited, {0, -1}, kSeed));
    TEST_CHECK(after.at(relative({0, -1}, ".bak")) == before.at(relative({0, -1})));
    TEST_CHECK(after.at("world.meta.bak") == before.at("world.meta"));
    for (const RegionCoord region : {RegionCoord{-18, 13}, {-1, 0}, {39, -79}}) {
        TEST_CHECK(after.at(relative(region)) == before.at(relative(region)));
        TEST_CHECK(!after.contains(relative(region, ".bak")));
    }
    const LoadedWorld loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.meta == later && loaded.modifications == edited);

    // A whole save removes the files of the regions without modifications.
    ModificationStore smaller = edited;
    smaller.clearBlock({20000, 0, -40000}); // region (39, -79)
    terrain::saveWorld(world, later, smaller);
    TEST_CHECK(!fs::exists(regionFile(world, {39, -79})));
    TEST_CHECK(terrain::savedRegions(world) == terrain::regionsOf(smaller));
    TEST_CHECK(terrain::loadWorld(world).modifications == smaller);
}

void testAbandonedTemporaries(const fs::path& dir) {
    const fs::path world = dir / "temporaries";
    const WorldMeta meta = storage_test::sampleMeta();
    const ModificationStore store = storage_test::sampleStore();
    terrain::saveWorld(world, meta, store);
    // Saves cut short by a crash: a half-written world.meta.tmp, garbage region .tmp files.
    const std::vector<std::uint8_t> metaBytes = fileBytes(world / "world.meta");
    writeBytes(world / "world.meta.tmp", std::span(metaBytes).first(metaBytes.size() / 2));
    const std::vector<std::uint8_t> garbage = {1, 2, 3, 4, 5};
    writeBytes(suffixed(regionFile(world, {0, -1}), ".tmp"), garbage);
    fs::create_directories(world / "regions" / "5.5");
    writeBytes(world / "regions" / "5.5" / "170.170.treg.tmp", garbage);

    const LoadedWorld loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.meta == meta && loaded.modifications == store && loaded.restored.empty());
    TEST_CHECK(terrain::savedRegions(world) == terrain::regionsOf(store));
    terrain::saveWorld(world, meta, store);
    bool temporaries = false;
    for (const auto& [name, bytes] : storage_test::filesUnder(world)) {
        temporaries = temporaries || name.ends_with(".tmp");
    }
    TEST_CHECK(!temporaries);
    TEST_CHECK(terrain::loadWorld(world).modifications == store);
}

void testFailedSave(const fs::path& dir) {
    const fs::path world = dir / "failed";
    const WorldMeta meta = storage_test::sampleMeta();
    const ModificationStore store = storage_test::sampleStore();
    terrain::saveWorld(world, meta, store);
    const auto saved = storage_test::filesUnder(world);
    // A directory where the new world.meta must be written: the save fails after writing the
    // new regions, before replacing any file.
    fs::create_directories(world / "world.meta.tmp" / "blocker");
    WorldMeta later = meta;
    later.playTimeSeconds = 99;
    ModificationStore edited = store;
    edited.setBlock({5, 5, 5}, BlockEdit::Added);
    TEST_CHECK(errorOf([&] { terrain::saveWorld(world, later, edited); }) ==
               StorageErrorKind::Io);
    TEST_CHECK(storage_test::filesUnder(world) == saved); // no file changed, no .tmp left
    const LoadedWorld loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.meta == meta && loaded.modifications == store && loaded.restored.empty());

    fs::remove_all(world / "world.meta.tmp");
    terrain::saveWorld(world, later, edited);
    const LoadedWorld retried = terrain::loadWorld(world);
    TEST_CHECK(retried.meta == later && retried.modifications == edited);
}

// Two saves, then the files of the second one damaged or lost.
void testBackups(const fs::path& dir) {
    const fs::path world = dir / "backups";
    const WorldMeta first = storage_test::sampleMeta();
    const ModificationStore oldStore = storage_test::sampleStore();
    terrain::saveWorld(world, first, oldStore);
    WorldMeta second = first;
    second.playTimeSeconds = 7200;
    ModificationStore newStore = oldStore;
    newStore.setBlock({4, 41, -2}, BlockEdit::Removed); // region (0, -1)
    terrain::saveWorld(world, second, newStore);

    const fs::path metaFile = world / "world.meta";
    const std::vector<std::uint8_t> goodMeta = fileBytes(metaFile);
    std::vector<std::uint8_t> damaged = goodMeta;
    damaged[20] ^= 0xFFU;
    writeBytes(metaFile, damaged);
    LoadedWorld loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.meta == first && loaded.modifications == newStore);
    TEST_CHECK(loaded.restored == std::vector<fs::path>{metaFile} && loaded.warnings.size() == 1);
    fs::remove(metaFile); // lost between the two renames of a save
    loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.meta == first && loaded.restored == std::vector<fs::path>{metaFile});
    writeBytes(metaFile, goodMeta);

    const fs::path region = regionFile(world, {0, -1});
    const fs::path regionBackup = suffixed(region, ".bak");
    const std::vector<std::uint8_t> goodRegion = fileBytes(region);
    writeBytes(region, std::span(goodRegion).first(goodRegion.size() / 2));
    loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.meta == second && loaded.modifications == oldStore);
    TEST_CHECK(loaded.restored == std::vector<fs::path>{region});
    // Its backup damaged too: a clear error naming both.
    const std::vector<std::uint8_t> goodBackup = fileBytes(regionBackup);
    writeBytes(regionBackup, std::vector<std::uint8_t>{1, 2, 3});
    std::string message;
    TEST_CHECK(loadError(world, &message) == StorageErrorKind::Truncated);
    TEST_CHECK(message.find(".treg") != std::string::npos &&
               message.find("backup") != std::string::npos);
    writeBytes(region, goodRegion);
    writeBytes(regionBackup, goodBackup);

    // A world.meta of a newer version is refused, never replaced by its backup.
    std::vector<std::uint8_t> newer = goodMeta;
    newer[4] = 3;
    writeBytes(metaFile, storage_test::resealed(newer));
    TEST_CHECK(loadError(world) == StorageErrorKind::NewerVersion);
    writeBytes(metaFile, goodMeta);
    loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.meta == second && loaded.modifications == newStore);
    TEST_CHECK(loaded.restored.empty());
}

void testErrors(const fs::path& dir) {
    TEST_CHECK(loadError(dir / "nowhere") == StorageErrorKind::Io);
    const fs::path world = dir / "errors";
    const WorldMeta meta = storage_test::sampleMeta();
    const ModificationStore store = storage_test::sampleStore();
    terrain::saveWorld(world, meta, store);
    WorldMeta invalid = meta;
    invalid.name.clear();
    TEST_CHECK(errorOf([&] { terrain::saveWorld(world, invalid, {}); }) ==
               StorageErrorKind::Invalid);
    TEST_CHECK(terrain::loadWorld(world).modifications == store); // nothing written

    // The regions of another world.
    WorldMeta other = meta;
    other.seed += 1;
    terrain::saveWorldMeta(world, other);
    std::string message;
    TEST_CHECK(loadError(world, &message) == StorageErrorKind::Invalid);
    TEST_CHECK(message.find("another world") != std::string::npos);
    terrain::saveWorldMeta(world, meta);

    // A region file in the directory of another group.
    const fs::path misplaced = world / "regions" / "0.0" / "39.-79.treg";
    fs::rename(regionFile(world, {39, -79}), misplaced);
    TEST_CHECK(errorOf([&] { static_cast<void>(terrain::savedRegions(world)); }) ==
               StorageErrorKind::Invalid);
    TEST_CHECK(loadError(world) == StorageErrorKind::Invalid);
    fs::rename(misplaced, regionFile(world, {39, -79}));

    // Unknown files are ignored, the reserved directories left alone.
    fs::create_directories(world / "players");
    writeBytes(world / "players" / "someone.dat", std::vector<std::uint8_t>{1});
    writeBytes(world / "regions" / ".DS_Store", std::vector<std::uint8_t>{1});
    writeBytes(world / "regions" / "0.-1" / "notes.txt", std::vector<std::uint8_t>{1});
    TEST_CHECK(terrain::loadWorld(world).modifications == store);
    terrain::saveWorld(world, meta, {});
    TEST_CHECK(terrain::savedRegions(world).empty());
    TEST_CHECK(fs::exists(world / "players" / "someone.dat"));
    TEST_CHECK(fs::exists(world / "regions" / ".DS_Store"));
    TEST_CHECK(fs::exists(world / "regions" / "0.-1" / "notes.txt"));
}

// Another program reading a region file while the world is saved (see testSaveWhileOpen of
// modifications_test): the save waits for a reader closing it soon; one holding it longer makes
// the save throw a StorageError (Io) on Windows, leaving a whole world that still loads.
void testSaveWhileOpen(const fs::path& dir) {
    const fs::path world = dir / "reader";
    const WorldMeta meta = storage_test::sampleMeta();
    const ModificationStore store = storage_test::sampleStore();
    terrain::saveWorld(world, meta, store);
    WorldMeta later = meta;
    later.playTimeSeconds += 60;
    ModificationStore edited = store;
    edited.setBlock({4, 41, -2}, BlockEdit::Removed); // region (0, -1)
    const fs::path region = regionFile(world, {0, -1});
    const std::vector<std::uint8_t> before = fileBytes(region);
    {
        std::ifstream reader(region, std::ios::binary);
        const auto failure = errorOf([&] { terrain::saveWorld(world, later, edited); });
        TEST_CHECK(!failure || failure == StorageErrorKind::Io);
        TEST_CHECK(storage_test::readAll(reader) == before);
        const LoadedWorld loaded = terrain::loadWorld(world);
        TEST_CHECK(loaded.restored.empty());
        TEST_CHECK(failure ? loaded.meta == meta && loaded.modifications == store
                           : loaded.meta == later && loaded.modifications == edited);
        bool temporaries = false;
        for (const auto& [name, bytes] : storage_test::filesUnder(world)) {
            temporaries = temporaries || name.ends_with(".tmp");
        }
        TEST_CHECK(!temporaries);
    }
    later.playTimeSeconds += 60;
    edited.setBlock({5, 41, -2}, BlockEdit::Removed);
    {
        const storage_test::BriefReader reader(region, std::chrono::milliseconds(60));
        const storage_test::BriefReader metaReader(world / "world.meta",
                                                   std::chrono::milliseconds(60));
        TEST_CHECK(!errorOf([&] { terrain::saveWorld(world, later, edited); }));
    }
    const LoadedWorld loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.meta == later && loaded.modifications == edited);
}

void testGeneratorChanged(const fs::path& dir) {
    const fs::path world = dir / "generator";
    WorldMeta meta = storage_test::sampleMeta();
    meta.generatorVersion = terrain::kGeneratorVersion + 1;
    terrain::saveWorld(world, meta, storage_test::sampleStore());
    const LoadedWorld loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.generatorChanged && loaded.warnings.size() == 1);
    TEST_CHECK(loaded.warnings.front() == terrain::generatorWarning(meta));
    TEST_CHECK(loaded.modifications == storage_test::sampleStore());
}

} // namespace

// A world of format 1 (phase 9): read, then saved in the current format, its files replaced.
void testFormatOneWorld(const fs::path& dir) {
    const fs::path world = dir / "format1";
    const WorldMeta meta = storage_test::sampleMeta();
    const ModificationStore store = storage_test::sampleStore();
    std::vector<std::uint8_t> oldMeta = terrain::serializeWorldMeta(meta);
    oldMeta[4] = 1;
    fs::create_directories(world);
    writeBytes(world / "world.meta", storage_test::resealed(oldMeta));
    for (const RegionCoord region : terrain::regionsOf(store)) {
        fs::create_directories(regionFile(world, region).parent_path());
        writeBytes(regionFile(world, region), storage_test::formatOneRegion(store, region));
    }
    const LoadedWorld loaded = terrain::loadWorld(world);
    TEST_CHECK(loaded.meta == meta && loaded.modifications == store && loaded.warnings.empty());
    terrain::saveWorld(world, loaded.meta, loaded.modifications);
    TEST_CHECK(fileBytes(world / "world.meta")[4] == terrain::kWorldFormatVersion);
    for (const RegionCoord region : terrain::regionsOf(store)) {
        TEST_CHECK(fileBytes(regionFile(world, region)) ==
                   terrain::serializeRegion(store, region, kSeed));
        TEST_CHECK(fileBytes(suffixed(regionFile(world, region), ".bak"))[4] == 1);
    }
    TEST_CHECK(terrain::loadWorld(world).modifications == store);
}

// A large destruction (filled and coded sections over a whole region), saved, then changed and
// saved again: its region keeps the previous version in its .bak, read when the file is damaged.
void testLargeDestruction(const fs::path& dir) {
    const fs::path world = dir / "destruction";
    const WorldMeta meta = storage_test::sampleMeta();
    ModificationStore first;
    for (int cz = 0; cz < terrain::kRegionChunks; ++cz) {
        for (int cx = 0; cx < terrain::kRegionChunks; ++cx) {
            for (int y = 2; y < 6; ++y) {
                first.setSection({cx, cz}, y, terrain::BlockSection::filled(BlockEdit::Removed));
            }
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    first.setBlock({cx * 16 + x, 31 - (x + z + cx) % 5, cz * 16 + z},
                                   BlockEdit::Removed);
                }
            }
        }
    }
    terrain::saveWorld(world, meta, first);
    ModificationStore second = first;
    second.setSection({3, 3}, 4, terrain::BlockSection::filled(BlockEdit::Added));
    terrain::saveWorldRegions(world, meta, second, std::vector<RegionCoord>{{0, 0}});
    TEST_CHECK(terrain::loadWorld(world).modifications == second);
    const fs::path region = regionFile(world, {0, 0});
    std::vector<std::uint8_t> damaged = fileBytes(region);
    damaged[damaged.size() / 2] ^= 0x40U;
    writeBytes(region, damaged);
    const LoadedWorld restored = terrain::loadWorld(world);
    TEST_CHECK(restored.modifications == first && restored.restored.size() == 1);
}

int main() {
    const fs::path dir = storage_test::freshDirectory("terrain_engine_world_save_test");
    testRoundTrip(dir);
    testIncrementalSave(dir);
    testAbandonedTemporaries(dir);
    testFailedSave(dir);
    testBackups(dir);
    testErrors(dir);
    testSaveWhileOpen(dir);
    testGeneratorChanged(dir);
    testFormatOneWorld(dir);
    testLargeDestruction(dir);
    fs::remove_all(dir);
    return test::result();
}
