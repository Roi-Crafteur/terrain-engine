// The experimental flags in the planet file (planet_file.hpp): a level built with flags is written
// with their text (version 2) and read back with them only; asked without them, or with others,
// the file is refused (PlanetFlagsError) and never built again over it, its bytes kept; without
// flags the file is that of main (version 1, planet_file_test keeps its bytes); a damaged file
// is told damaged, not of other flags.

#include "terrain/storage/planet_file.hpp"

#include "planet_golden_support.hpp"
#include "storage_test_support.hpp"

#include "test_assert.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using terrain::PlanetFlagsError;
using terrain::PlanetLevel;
using terrain::StorageError;
using terrain::StorageErrorKind;

terrain::PlanetParams params() {
    return golden::smallPlanetParams(2);
}

std::uint32_t versionOf(const std::filesystem::path& file) {
    const std::vector<std::uint8_t> bytes = storage_test::fileBytes(file);
    return static_cast<std::uint32_t>(bytes[4]) | static_cast<std::uint32_t>(bytes[5]) << 8 |
           static_cast<std::uint32_t>(bytes[6]) << 16 | static_cast<std::uint32_t>(bytes[7]) << 24;
}

// The flags the load of file with wanted is refused for ("" when it is read; "!" for another
// error).
std::string refusedFor(const std::filesystem::path& file, const std::string& wanted) {
    try {
        (void)terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, params(),
                                      golden::kSmallPlanet, wanted);
    } catch (const PlanetFlagsError& e) {
        TEST_CHECK(e.wanted() == wanted && e.kind() == StorageErrorKind::Invalid);
        TEST_CHECK(std::string(e.what()).find("experimental flags") != std::string::npos);
        return e.stored().empty() ? "(none)" : e.stored();
    } catch (const StorageError&) {
        return "!";
    }
    return "";
}

void testFlagged(const std::filesystem::path& file, std::uint64_t checksum) {
    TEST_CHECK(versionOf(file) == 2);
    TEST_CHECK(refusedFor(file, "seas").empty());
    TEST_CHECK(refusedFor(file, "") == "seas");
    TEST_CHECK(refusedFor(file, "lows,seas") == "seas");
    // Never built again over it: the same bytes after a refused open.
    const std::vector<std::uint8_t> before = storage_test::fileBytes(file);
    bool refused = false;
    try {
        (void)terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed,
                                       terrain::HeightfieldParams{}, params(),
                                       golden::kSmallPlanet);
    } catch (const PlanetFlagsError& e) {
        refused = e.stored() == "seas" && e.wanted().empty();
    }
    TEST_CHECK(refused && storage_test::fileBytes(file) == before);
    bool built = true;
    const PlanetLevel again = terrain::openPlanetLevel(
        file, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, params(),
        golden::kSmallPlanet, &built, {}, "seas");
    TEST_CHECK(!built && again.checksum() == checksum);
}

void testPlain(const std::filesystem::path& file, const PlanetLevel& level) {
    terrain::savePlanetFile(file, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet);
    TEST_CHECK(versionOf(file) == 1);
    TEST_CHECK(refusedFor(file, "").empty());
    TEST_CHECK(refusedFor(file, "seas") == "(none)");
}

void testDamaged(const std::filesystem::path& file, const PlanetLevel& level) {
    terrain::savePlanetFile(file, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet,
                            "seas");
    std::vector<std::uint8_t> bytes = storage_test::fileBytes(file);
    bytes[bytes.size() / 2] ^= 0x10;
    storage_test::writeBytes(file, bytes);
    TEST_CHECK(refusedFor(file, "") == "!");
    TEST_CHECK(refusedFor(file, "seas") == "!");
}

} // namespace

int main() {
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_planet_flags_test");
    const std::filesystem::path file = dir / terrain::kPlanetFileName;
    bool built = false;
    const PlanetLevel level = terrain::openPlanetLevel(
        file, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, params(),
        golden::kSmallPlanet, &built, {}, "seas");
    TEST_CHECK(built && std::filesystem::exists(file));
    testFlagged(file, level.checksum());
    testPlain(dir / "plain.bin", level);
    testDamaged(dir / "damaged.bin", level);
    std::filesystem::remove_all(dir);
    return test::result();
}
