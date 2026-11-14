// The planet file of version 2 (planet_file.hpp, planet_sections.hpp): a level built with
// experimental flags is written with their text and read back with them only; asked without
// them, or with others, the file is refused (PlanetFlagsError) and never built again over it, its
// bytes kept; a section is written after the level and read back; a section unknown to the reader
// makes the file stale (built again, never read wrong); a section read wrong is refused; without
// flags nor sections the file is that of main (version 1, planet_file_test keeps its bytes); a
// damaged file is told damaged, not of other flags.

#include "terrain/storage/planet_file.hpp"

#include "planet_sections.hpp"

#include "planet_golden_support.hpp"
#include "storage_test_support.hpp"

#include "test_assert.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using terrain::PlanetFlagsError;
using terrain::PlanetLevel;
using terrain::StorageError;
using terrain::StorageErrorKind;
namespace storage = terrain::storage;

terrain::PlanetParams params() {
    return golden::smallPlanetParams(2);
}

std::uint32_t versionOf(const std::filesystem::path& file) {
    const std::vector<std::uint8_t> bytes = storage_test::fileBytes(file);
    return static_cast<std::uint32_t>(bytes[4]) | static_cast<std::uint32_t>(bytes[5]) << 8 |
           static_cast<std::uint32_t>(bytes[6]) << 16 | static_cast<std::uint32_t>(bytes[7]) << 24;
}

// How a load of file with wanted ends: "" read, the stored flags ("(none)" for "") refused for
// its flags, "stale" or "invalid" for another Invalid, "!" for any other error.
std::string loadOf(const std::filesystem::path& file, const std::string& wanted,
                   std::span<const storage::PlanetSection> table = storage::planetSections()) {
    try {
        (void)storage::loadPlanetFile(file, terrain::kDefaultWorldSeed, params(),
                                      golden::kSmallPlanet, wanted, table);
    } catch (const PlanetFlagsError& e) {
        TEST_CHECK(e.wanted() == wanted && e.kind() == StorageErrorKind::Invalid);
        TEST_CHECK(std::string(e.what()).find("experimental flags") != std::string::npos);
        return e.stored().empty() ? "(none)" : e.stored();
    } catch (const StorageError& e) {
        if (e.kind() != StorageErrorKind::Invalid) {
            return "!";
        }
        return std::string(e.what()).find("stale") != std::string::npos ? "stale" : "invalid";
    }
    return "";
}

// The section of test: present while gPresent, a word and the count of the cells of the level.
bool gPresent = true;
std::uint32_t gWord = 0;
constexpr std::uint32_t kWord = 0xC0FFEEU;

void writeTest(storage::ByteWriter& out, const PlanetLevel& level) {
    out.u32(kWord);
    out.u32(static_cast<std::uint32_t>(level.cells().elevation.size()));
}

const std::array<storage::PlanetSection, 1> kSections = {{
    {{'T', 'E', 'S', 'T'}, [](const PlanetLevel&) { return gPresent; }, writeTest,
     [](storage::ByteReader& in, PlanetLevel::Parts& parts) {
         gWord = in.u32();
         TEST_CHECK(in.u32() == parts.cells.elevation.size());
     }},
}};
// The same section read wrong: a word left unread, or one more read than written.
const std::array<storage::PlanetSection, 1> kShort = {{
    {{'T', 'E', 'S', 'T'}, [](const PlanetLevel&) { return true; }, writeTest,
     [](storage::ByteReader& in, PlanetLevel::Parts&) { (void)in.u32(); }},
}};
const std::array<storage::PlanetSection, 1> kLong = {{
    {{'T', 'E', 'S', 'T'}, [](const PlanetLevel&) { return true; }, writeTest,
     [](storage::ByteReader& in, PlanetLevel::Parts&) {
         for (int i = 0; i < 3; ++i) {
             (void)in.u32();
         }
     }},
}};

void testFlagged(const std::filesystem::path& file, std::uint64_t checksum) {
    TEST_CHECK(versionOf(file) == 2);
    TEST_CHECK(loadOf(file, "alpha").empty());
    TEST_CHECK(loadOf(file, "") == "alpha");
    TEST_CHECK(loadOf(file, "alpha,beta") == "alpha");
    // Never built again over it: the same bytes after a refused open.
    const std::vector<std::uint8_t> before = storage_test::fileBytes(file);
    bool refused = false;
    try {
        (void)terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed,
                                       terrain::HeightfieldParams{}, params(),
                                       golden::kSmallPlanet);
    } catch (const PlanetFlagsError& e) {
        refused = e.stored() == "alpha" && e.wanted().empty();
    }
    TEST_CHECK(refused && storage_test::fileBytes(file) == before);
    bool built = true;
    const PlanetLevel again = terrain::openPlanetLevel(
        file, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, params(),
        golden::kSmallPlanet, &built, {}, "alpha");
    TEST_CHECK(!built && again.checksum() == checksum);
}

void testPlain(const std::filesystem::path& dir, const PlanetLevel& level) {
    const std::filesystem::path plain = dir / "plain.bin";
    terrain::savePlanetFile(plain, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet);
    TEST_CHECK(versionOf(plain) == 1);
    TEST_CHECK(loadOf(plain, "").empty());
    TEST_CHECK(loadOf(plain, "alpha") == "(none)");
    // A section that is not present: the same bytes as without any section.
    gPresent = false;
    const std::filesystem::path absent = dir / "absent.bin";
    storage::savePlanetFile(absent, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet, "",
                            kSections);
    gPresent = true;
    TEST_CHECK(storage_test::fileBytes(absent) == storage_test::fileBytes(plain));
}

void testSections(const std::filesystem::path& dir, const PlanetLevel& level) {
    const std::filesystem::path file = dir / "sections.bin";
    storage::savePlanetFile(file, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet, "",
                            kSections);
    TEST_CHECK(versionOf(file) == 2);
    gWord = 0;
    TEST_CHECK(loadOf(file, "", kSections).empty() && gWord == kWord);
    TEST_CHECK(loadOf(file, "alpha", kSections) == "(none)");
    TEST_CHECK(loadOf(file, "") == "stale"); // a reader without the section
    TEST_CHECK(loadOf(file, "", kShort) == "invalid");
    TEST_CHECK(loadOf(file, "", kLong) == "invalid");
    // Stale: built again (no section, version 1), never read wrong.
    bool built = false;
    const PlanetLevel again =
        terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                 params(), golden::kSmallPlanet, &built);
    TEST_CHECK(built && again.checksum() == level.checksum() && versionOf(file) == 1);
}

void testDamaged(const std::filesystem::path& file, const PlanetLevel& level) {
    terrain::savePlanetFile(file, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet,
                            "alpha");
    std::vector<std::uint8_t> bytes = storage_test::fileBytes(file);
    bytes[bytes.size() / 2] ^= 0x10;
    storage_test::writeBytes(file, bytes);
    TEST_CHECK(loadOf(file, "") == "!");
    TEST_CHECK(loadOf(file, "alpha") == "!");
}

} // namespace

int main() {
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_planet_flags_test");
    const std::filesystem::path file = dir / terrain::kPlanetFileName;
    bool built = false;
    const PlanetLevel level = terrain::openPlanetLevel(
        file, terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, params(),
        golden::kSmallPlanet, &built, {}, "alpha");
    TEST_CHECK(built && std::filesystem::exists(file));
    testFlagged(file, level.checksum());
    testPlain(dir, level);
    testSections(dir, level);
    testDamaged(dir / "damaged.bin", level);
    std::filesystem::remove_all(dir);
    return test::result();
}
