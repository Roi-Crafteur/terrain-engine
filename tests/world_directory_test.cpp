// The worlds of a directory: folder names derived from the world names, creation (two worlds of
// the same name get two folders), listing (names, seeds, dates; damaged worlds listed with their
// error; other folders and files ignored), renaming (the name only, the folder stays) and
// deletion, refused for anything that is not a world folder.
#include "terrain/storage/world_directory.hpp"
#include "terrain/storage/world_save.hpp"

#include "storage_test_support.hpp"
#include "test_assert.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

using storage_test::errorOf;
using terrain::StorageErrorKind;
using terrain::WorldEntry;
using terrain::WorldMeta;

WorldMeta world(const std::string& name, std::uint64_t seed, std::int64_t createdAt) {
    WorldMeta meta;
    meta.name = name;
    meta.seed = seed;
    meta.createdAt = createdAt;
    meta.savedAt = createdAt + 10;
    return meta;
}

void testFolderNames() {
    TEST_CHECK(terrain::worldFolderName("Mon Monde") == "mon_monde");
    TEST_CHECK(terrain::worldFolderName(storage_test::sampleMeta().name) == "vall_e_des_brumes");
    TEST_CHECK(terrain::worldFolderName("  Les -- 3 (îles)!  ") == "les_--_3_les");
    TEST_CHECK(terrain::worldFolderName("a/b\\c:..d") == "a_b_c_d");
    TEST_CHECK(terrain::worldFolderName("!!!") == "world");
    TEST_CHECK(terrain::worldFolderName("\xC3\x89t\xC3\xA9") == "t"); // "Été"
    TEST_CHECK(terrain::worldFolderName("CON") == "con_");
    TEST_CHECK(terrain::worldFolderName("Com1") == "com1_");
    TEST_CHECK(terrain::worldFolderName("com10") == "com10");
    TEST_CHECK(terrain::worldFolderName(std::string(40, 'x')) == std::string(32, 'x'));
    TEST_CHECK(terrain::worldFolderName(std::string(31, 'x') + " y") == std::string(31, 'x'));
}

std::vector<std::string> folders(const std::vector<WorldEntry>& worlds) {
    std::vector<std::string> names;
    for (const WorldEntry& entry : worlds) {
        names.push_back(entry.folder);
    }
    return names;
}

void testCreateAndList(const fs::path& root) {
    TEST_CHECK(terrain::listWorlds(root).empty()); // root does not exist yet
    TEST_CHECK(terrain::createWorld(root, world("Mon Monde", 1, 100)) == "mon_monde");
    TEST_CHECK(terrain::createWorld(root, world("Mon Monde", 2, 200)) == "mon_monde-2");
    TEST_CHECK(terrain::createWorld(root, world("CON", 3, 300)) == "con_");
    TEST_CHECK(errorOf([&] {
                   static_cast<void>(terrain::createWorld(root, world("", 4, 400)));
               }) == StorageErrorKind::Invalid);
    // Not worlds: a folder without world.meta, a file. A damaged world is listed with its error.
    fs::create_directories(root / "notes");
    storage_test::writeBytes(root / "readme.txt", std::vector<std::uint8_t>{1});
    fs::create_directories(root / "broken");
    storage_test::writeBytes(root / "broken" / "world.meta", std::vector<std::uint8_t>{1, 2});

    const std::vector<WorldEntry> worlds = terrain::listWorlds(root);
    TEST_CHECK(folders(worlds) ==
               (std::vector<std::string>{"broken", "con_", "mon_monde", "mon_monde-2"}));
    if (worlds.size() == 4) {
        TEST_CHECK(!worlds[0].meta && worlds[0].error.find("world.meta") != std::string::npos);
        TEST_CHECK(worlds[2].meta && worlds[2].meta->name == "Mon Monde" &&
                   worlds[2].meta->seed == 1 && worlds[2].meta->createdAt == 100 &&
                   worlds[2].meta->savedAt == 110 && worlds[2].error.empty());
        TEST_CHECK(worlds[3].meta && worlds[3].meta->name == "Mon Monde" &&
                   worlds[3].meta->seed == 2 && !worlds[3].restored);
        TEST_CHECK(worlds[1].meta && worlds[1].meta->name == "CON");
    }
    // A new world holds its metadata and no modification.
    const terrain::LoadedWorld loaded = terrain::loadWorld(root / "mon_monde-2");
    TEST_CHECK(loaded.meta == world("Mon Monde", 2, 200) && loaded.modifications.empty());
}

void testRename(const fs::path& root) {
    const WorldMeta before = terrain::loadWorldMeta(root / "mon_monde").meta;
    terrain::renameWorld(root, "mon_monde", "Nouveau nom");
    WorldMeta expected = before;
    expected.name = "Nouveau nom";
    TEST_CHECK(terrain::loadWorldMeta(root / "mon_monde").meta == expected);
    TEST_CHECK(errorOf([&] { terrain::renameWorld(root, "mon_monde", "bad\x01"); }) ==
               StorageErrorKind::Invalid);
    TEST_CHECK(errorOf([&] { terrain::renameWorld(root, "../mon_monde", "x"); }) ==
               StorageErrorKind::Invalid);
    TEST_CHECK(errorOf([&] { terrain::renameWorld(root, "missing", "x"); }) ==
               StorageErrorKind::Io);
    TEST_CHECK(terrain::loadWorldMeta(root / "mon_monde").meta == expected);
}

void testDelete(const fs::path& root) {
    for (const char* refused : {"", ".", "..", "../mon_monde", "a/b", "a\\b", "notes",
                                "readme.txt", "broken", "missing"}) {
        TEST_CHECK(errorOf([&] { terrain::deleteWorld(root, refused); }) ==
                   StorageErrorKind::Invalid);
    }
    std::error_code error;
    fs::create_directory_symlink(root / "con_", root / "link", error);
    if (!error) { // symbolic links may be unavailable (Windows without privileges)
        TEST_CHECK(errorOf([&] { terrain::deleteWorld(root, "link"); }) ==
                   StorageErrorKind::Invalid);
        fs::remove(root / "link");
    }
    TEST_CHECK(fs::exists(root / "notes") && fs::exists(root / "broken"));
    TEST_CHECK(fs::exists(root / "con_" / "world.meta"));

    terrain::deleteWorld(root, "mon_monde-2");
    TEST_CHECK(!fs::exists(root / "mon_monde-2"));
    terrain::renameWorld(root, "con_", "Renamed"); // a world.meta.bak too
    terrain::deleteWorld(root, "con_");
    TEST_CHECK(!fs::exists(root / "con_"));
    TEST_CHECK(folders(terrain::listWorlds(root)) ==
               (std::vector<std::string>{"broken", "mon_monde"}));
    // A deleted name is free again.
    TEST_CHECK(terrain::createWorld(root, world("Mon Monde", 5, 500)) == "mon_monde-2");
}

} // namespace

int main() {
    testFolderNames();
    const fs::path dir = storage_test::freshDirectory("terrain_engine_world_directory_test");
    const fs::path root = dir / "worlds";
    testCreateAndList(root);
    testRename(root);
    testDelete(root);
    fs::remove_all(dir);
    return test::result();
}
