// The provenance of the planet file (agent 55, task T4 of the audit of agent 53): the fingerprint
// of the relief a level is built from (relief_fingerprint.hpp), in the section PROV of the file.
// - A level built holds it, outside its checksum (the same level, the same checksum).
// - The two refusals, on the same file: asked with other experimental flags, the file is refused
//   (PlanetFlagsError, agent 51) and never written over; asked with another relief at the same
//   kReliefGeneratorVersion, it is stale, built again, and the old file is kept as <file>.bak.
// - A file without the section (written before it) is read: provenance unknown, never rebuilt.
// - The serialized parameters of the default relief and the fingerprint of the small world are
//   pinned: a change of them is a change of the relief.
#include "terrain/generation/relief_fingerprint.hpp"
#include "terrain/storage/planet_file.hpp"

#include "planet_sections.hpp"
#include "probe_world.hpp"

#include "planet_golden_support.hpp"
#include "storage_test_support.hpp"

#include "test_assert.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace {

using terrain::PlanetLevel;
namespace storage = terrain::storage;

// Pinned on main 57e7004 with #81: the bytes of the parameters of the default relief (their
// count and their FNV-1a 64) and the fingerprint of the default relief on the small world.
constexpr std::size_t kParamsSize = 853;
constexpr std::uint64_t kParamsHash = 0xA48B612451C20AAFULL;
constexpr std::uint64_t kSmallFingerprint = 0x7A7C1E0D82DC47BEULL;

terrain::PlanetParams params() {
    return golden::smallPlanetParams(2);
}

std::uint64_t fnv(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const std::uint8_t b : bytes) {
        h = (h ^ b) * 0x100000001B3ULL;
    }
    return h;
}

// Another relief at the same kReliefGeneratorVersion: a little more land.
terrain::HeightfieldParams otherRelief() {
    terrain::HeightfieldParams p;
    p.geography.landShare = 0.38f;
    return p;
}

std::uint32_t versionOf(const std::filesystem::path& file) {
    const std::vector<std::uint8_t> bytes = storage_test::fileBytes(file);
    return static_cast<std::uint32_t>(bytes[4]) | static_cast<std::uint32_t>(bytes[5]) << 8 |
           static_cast<std::uint32_t>(bytes[6]) << 16 | static_cast<std::uint32_t>(bytes[7]) << 24;
}

// The status and the provenance terrain_probe --planet-check gives for file.
probe::PlanetCheck checkOf(const std::filesystem::path& file) {
    probe::Options o;
    o.planetCheck = file;
    o.world = golden::kSmallPlanet;
    o.threads = 2;
    return probe::checkPlanet(o);
}

void testPinned() {
    const std::vector<std::uint8_t> bytes = terrain::reliefParamsBytes({});
    const terrain::Heightfield small(terrain::kDefaultWorldSeed, {}, golden::kSmallPlanet);
    const std::uint64_t fingerprint = terrain::reliefFingerprint(small);
    std::printf("default relief: %zu bytes of parameters, FNV 0x%016llx; fingerprint of the small "
                "world 0x%016llx\n",
                bytes.size(), static_cast<unsigned long long>(fnv(bytes)),
                static_cast<unsigned long long>(fingerprint));
    TEST_CHECK(bytes.size() == kParamsSize);
    TEST_CHECK(fnv(bytes) == kParamsHash);
    TEST_CHECK(fingerprint == kSmallFingerprint);
    // Another parameter, another fingerprint; the same relief, the same one.
    const terrain::Heightfield other(terrain::kDefaultWorldSeed, otherRelief(),
                                     golden::kSmallPlanet);
    TEST_CHECK(terrain::reliefFingerprint(other) != fingerprint);
    TEST_CHECK(terrain::reliefParamsBytes(otherRelief()) != bytes);
}

// A level built holds the fingerprint of its relief, written in PROV and read back; the level is
// the same (its checksum) as without it.
PlanetLevel testWritten(const std::filesystem::path& file) {
    bool built = false;
    const PlanetLevel level = terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed, {},
                                                       params(), golden::kSmallPlanet, &built);
    const terrain::Heightfield relief(terrain::kDefaultWorldSeed, {}, golden::kSmallPlanet);
    TEST_CHECK(built && level.reliefFingerprint() == terrain::reliefFingerprint(relief));
    TEST_CHECK(versionOf(file) == 2);
    const PlanetLevel read =
        terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, params(), golden::kSmallPlanet);
    TEST_CHECK(read.reliefFingerprint() == level.reliefFingerprint());
    TEST_CHECK(read.checksum() == level.checksum());
    const probe::PlanetCheck check = checkOf(file);
    TEST_CHECK(check.status == "ok" && check.provenance == "ok");
    return level;
}

// The two refusals on the same file: other flags, never written over; another relief, stale,
// built again, the old file kept as .bak.
void testRefusals(const std::filesystem::path& file) {
    const std::vector<std::uint8_t> before = storage_test::fileBytes(file);
    std::filesystem::path backup = file;
    backup += ".bak";
    std::filesystem::remove(backup);
    bool flags = false;
    try {
        (void)terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed, {}, params(),
                                       golden::kSmallPlanet, nullptr, {}, "alpha");
    } catch (const terrain::PlanetFlagsError& e) {
        flags = e.stored().empty() && e.wanted() == "alpha";
    }
    TEST_CHECK(flags && storage_test::fileBytes(file) == before);
    TEST_CHECK(!std::filesystem::exists(backup));
    bool stale = false;
    try {
        (void)terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, params(),
                                      golden::kSmallPlanet, "", otherRelief());
    } catch (const terrain::StorageError& e) {
        stale = e.kind() == terrain::StorageErrorKind::Invalid &&
                std::string(e.what()).find("stale: the planet level of another relief") !=
                    std::string::npos;
    }
    TEST_CHECK(stale);
    bool built = false;
    const PlanetLevel other = terrain::openPlanetLevel(
        file, terrain::kDefaultWorldSeed, otherRelief(), params(), golden::kSmallPlanet, &built);
    TEST_CHECK(built && std::filesystem::exists(backup));
    TEST_CHECK(storage_test::fileBytes(backup) == before);
    const terrain::Heightfield relief(terrain::kDefaultWorldSeed, otherRelief(),
                                      golden::kSmallPlanet);
    TEST_CHECK(other.reliefFingerprint() == terrain::reliefFingerprint(relief));
    std::printf("the two refusals: other flags refused, the file kept; another relief stale, built "
                "again, the old file kept as %s\n",
                backup.filename().string().c_str());
}

// A file written before the fingerprint (no section PROV): read, provenance unknown, not built
// again (its level is the same).
void testUnknown(const std::filesystem::path& file, const PlanetLevel& level) {
    const std::span<const storage::PlanetSection> none;
    storage::savePlanetFile(file, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet, "",
                            none);
    TEST_CHECK(versionOf(file) == 1);
    bool built = true;
    const PlanetLevel read = terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed, {},
                                                      params(), golden::kSmallPlanet, &built);
    TEST_CHECK(!built && !read.reliefFingerprint() && read.checksum() == level.checksum());
    const probe::PlanetCheck check = checkOf(file);
    TEST_CHECK(check.status == "ok" && check.provenance == "unknown");
}

} // namespace

int main() {
    testPinned();
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_planet_provenance");
    const std::filesystem::path file = dir / terrain::kPlanetFileName;
    const PlanetLevel level = testWritten(file);
    testRefusals(file);
    testUnknown(dir / "before.bin", level);
    std::filesystem::remove_all(dir);
    return test::result();
}
