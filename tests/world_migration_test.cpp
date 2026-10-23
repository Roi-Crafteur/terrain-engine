// Migration of the modification files of the previous format (TMOD v1) to worlds: a TMOD file
// saved before the world format, kept byte for byte here, and one written by saveModifications
// become worlds holding the same seed and modifications, with the legacy generator version; the
// TMOD file is left as it was; an invalid file or meta creates no world.
#include "terrain/storage/world_directory.hpp"
#include "terrain/storage/world_save.hpp"

#include "storage_test_support.hpp"
#include "test_assert.hpp"

#include <filesystem>
#include <vector>

namespace {

namespace fs = std::filesystem;

using storage_test::errorOf;
using terrain::ModificationStore;
using terrain::StorageErrorKind;
using terrain::WorldMeta;

// A TMOD v1 file: seed 7, a removed plant and an added flower in chunk (-1, -1), an added block
// in chunk (0, 0).
const std::vector<std::uint8_t> kTmodFile = {
    // header: magic, version 1, seed 7, 2 chunks
    0x54, 0x4D, 0x4F, 0x44, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    // chunk (-1, -1): a removed plant and a flower
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0xBF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD0, 0xBF, 0x01, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xBF, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x80,
    0x3F, 0x00, 0x00, 0x00, 0x00,
    // chunk (0, 0): block (1, 2, 3) added
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // FNV-1a
    0x64, 0x3C, 0x1E, 0xE7,
};

ModificationStore tmodStore() {
    ModificationStore store;
    store.setBlock({1, 2, 3}, terrain::BlockEdit::Added);
    store.removeGeneratedPlant({-0.5, -0.25});
    store.addPlant({terrain::PlantType::Flower, -1.0, -2.0, 3.0f, 1.0f, 0.0f});
    return store;
}

WorldMeta migrationMeta() {
    WorldMeta meta;
    meta.name = "Ancien monde";
    meta.seed = 999;                      // replaced by the seed of the file
    meta.generatorVersion = 12345;        // replaced by the legacy generator
    meta.createdAt = 1'760'000'000;
    meta.savedAt = 1'790'000'000;
    meta.spawn = {10.0, 70.0, -20.0};
    return meta;
}

void testFixture(const fs::path& dir) {
    const fs::path file = dir / "terrain_modifications.bin";
    storage_test::writeBytes(file, kTmodFile);
    const fs::path root = dir / "worlds";
    const std::string folder = terrain::migrateModificationFile(file, root, migrationMeta());
    TEST_CHECK(folder == "ancien_monde");
    TEST_CHECK(storage_test::fileBytes(file) == kTmodFile); // left as it was

    const terrain::LoadedWorld world = terrain::loadWorld(root / folder);
    WorldMeta expected = migrationMeta();
    expected.seed = 7;
    expected.generatorVersion = terrain::kLegacyGeneratorVersion;
    TEST_CHECK(world.meta == expected);
    TEST_CHECK(world.modifications == tmodStore());
    TEST_CHECK(terrain::serializeModifications(tmodStore(), 7) == kTmodFile);
    // Up to phase 8C-2, the legacy generator is the current one: no warning yet.
    TEST_CHECK(world.generatorChanged ==
               (terrain::kLegacyGeneratorVersion != terrain::kGeneratorVersion));
    TEST_CHECK(terrain::savedRegions(root / folder) ==
               (std::vector<terrain::RegionCoord>{{-1, -1}, {0, 0}}));
    // Written in the current format.
    TEST_CHECK(storage_test::fileBytes(root / folder / terrain::regionRelativePath({0, 0}))[4] ==
               terrain::kWorldFormatVersion);
}

void testSavedFile(const fs::path& dir) {
    const fs::path file = dir / "sample.bin";
    terrain::saveModifications(file, storage_test::sampleStore(), storage_test::kSeed);
    const std::vector<std::uint8_t> bytes = storage_test::fileBytes(file);
    const fs::path root = dir / "worlds";
    const std::string folder = terrain::migrateModificationFile(file, root, migrationMeta());
    TEST_CHECK(folder == "ancien_monde-2"); // the first migration holds "ancien_monde"
    const terrain::LoadedWorld world = terrain::loadWorld(root / folder);
    TEST_CHECK(world.meta.seed == storage_test::kSeed);
    TEST_CHECK(world.modifications == storage_test::sampleStore());
    TEST_CHECK(storage_test::fileBytes(file) == bytes);
    TEST_CHECK(terrain::listWorlds(root).size() == 2);
}

void testInvalid(const fs::path& dir) {
    const fs::path root = dir / "refused";
    const auto migrationError = [&root](const fs::path& file, const WorldMeta& meta) {
        return errorOf([&] {
            static_cast<void>(terrain::migrateModificationFile(file, root, meta));
        });
    };
    std::vector<std::uint8_t> damaged = kTmodFile;
    damaged[30] ^= 0x01U;
    const fs::path file = dir / "damaged.bin";
    storage_test::writeBytes(file, damaged);
    TEST_CHECK(migrationError(file, migrationMeta()) == StorageErrorKind::Corrupt);
    TEST_CHECK(migrationError(dir / "missing.bin", migrationMeta()) == StorageErrorKind::Io);
    storage_test::writeBytes(file, kTmodFile);
    WorldMeta unnamed = migrationMeta();
    unnamed.name.clear();
    TEST_CHECK(migrationError(file, unnamed) == StorageErrorKind::Invalid);
    TEST_CHECK(!fs::exists(root) || fs::is_empty(root)); // no world left behind
}

} // namespace

int main() {
    const fs::path dir = storage_test::freshDirectory("terrain_engine_world_migration_test");
    testFixture(dir);
    testSavedFile(dir);
    testInvalid(dir);
    fs::remove_all(dir);
    return test::result();
}
