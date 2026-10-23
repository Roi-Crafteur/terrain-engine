// The planet file: the level of the small world written and read back to the bit, built only
// when the file does not hold it, refused when it holds another world, damaged or truncated.
#include "terrain/storage/planet_file.hpp"

#include "planet_golden_support.hpp"
#include "storage_test_support.hpp"

#include "test_assert.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace {

using terrain::PlanetLevel;
using terrain::StorageError;
using terrain::StorageErrorKind;

terrain::PlanetParams params() {
    return golden::smallPlanetParams(2);
}

template <class F> StorageErrorKind errorOf(F&& f) {
    try {
        f();
    } catch (const StorageError& e) {
        return e.kind();
    }
    return StorageErrorKind::Io; // none: Io is never expected below
}

void testRoundTrip(const std::filesystem::path& file, const PlanetLevel& level) {
    terrain::savePlanetFile(file, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet);
    const PlanetLevel again =
        terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, params(), golden::kSmallPlanet);
    TEST_CHECK(again.checksum() == level.checksum());
    TEST_CHECK(again.rivers().segments.size() == level.rivers().segments.size() &&
               again.lakes().size() == level.lakes().size() &&
               again.passes().raised == level.passes().raised &&
               again.breach().breached == level.breach().breached);
    bool built = true;
    const PlanetLevel opened =
        terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                 params(), golden::kSmallPlanet, &built);
    TEST_CHECK(!built && opened.checksum() == level.checksum());
}

void testStale(const std::filesystem::path& file) {
    TEST_CHECK(errorOf([&] {
                   (void)terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed + 1, params(),
                                                 golden::kSmallPlanet);
               }) == StorageErrorKind::Invalid);
    terrain::PlanetParams other = params();
    other.rivers.minDischarge = 20.0;
    TEST_CHECK(errorOf([&] {
                   (void)terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, other,
                                                 golden::kSmallPlanet);
               }) == StorageErrorKind::Invalid);
    const terrain::WorldSize wider{golden::kSmallPlanet.x * 2.0, golden::kSmallPlanet.z};
    TEST_CHECK(errorOf([&] {
                   (void)terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, params(),
                                                 wider);
               }) == StorageErrorKind::Invalid);
}

void testDamaged(const std::filesystem::path& file, const PlanetLevel& level) {
    std::vector<std::uint8_t> bytes = storage_test::fileBytes(file);
    std::vector<std::uint8_t> flipped = bytes;
    flipped[flipped.size() / 2] ^= 0x10;
    storage_test::writeBytes(file, flipped);
    const auto load = [&] {
        (void)terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, params(),
                                      golden::kSmallPlanet);
    };
    TEST_CHECK(errorOf(load) == StorageErrorKind::Corrupt);
    bytes.resize(bytes.size() / 3);
    storage_test::writeBytes(file, bytes);
    TEST_CHECK(errorOf(load) == StorageErrorKind::Truncated);
    // A damaged file is built again, and written whole.
    bool built = false;
    const PlanetLevel rebuilt =
        terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                 params(), golden::kSmallPlanet, &built);
    TEST_CHECK(built && rebuilt.checksum() == level.checksum());
    TEST_CHECK(terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, params(),
                                       golden::kSmallPlanet)
                   .checksum() == level.checksum());
}

} // namespace

int main() {
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_planet_file_test");
    const std::filesystem::path file = dir / terrain::kPlanetFileName;
    bool built = false;
    const PlanetLevel level =
        terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                 params(), golden::kSmallPlanet, &built);
    TEST_CHECK(built && std::filesystem::exists(file));
    testRoundTrip(file, level);
    testStale(file);
    testDamaged(file, level);
    std::filesystem::remove_all(dir);
    return test::result();
}
