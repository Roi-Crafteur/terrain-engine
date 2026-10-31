// What a world keeps of its settlement (storage/settlement_file.hpp), on the default world in cells
// of 65,536 m: the record written and read back the same; its fingerprint, the one of the data;
// the kingdoms computed again from the copy alone, with the global data changed since, the same to
// the bit; an altered copy, a damaged fingerprint, a missing member and a newer format refused
// with a clear message.

#include "settlement_world_support.hpp"
#include "storage_test_support.hpp"
#include "terrain/generation/settlement/kingdoms.hpp"
#include "terrain/storage/settlement_file.hpp"
#include "terrain/storage/storage_error.hpp"

#include "test_assert.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace st = terrain::settlement;
namespace fs = std::filesystem;

namespace {

st::Kingdoms kingdomsOf(const settlement_test::World& w, const st::SettlementData& data,
                        const st::SettlementRequest& request) {
    const st::SettlementMaps m =
        st::computeSettlementMaps(w.level, &w.field, data, settlement_test::kSeed, 0);
    const st::SuitabilityMaps s =
        st::computeSuitability(w.level, m, data, settlement_test::kSeed, 0);
    const st::ExpansionTerrain t =
        st::buildExpansionTerrain(w.level, m, data.config, settlement_test::kSeed, 0);
    return st::buildKingdoms(w.level, m, s, t, data, request, settlement_test::kSeed, 0);
}

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeText(const fs::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
}

// The file with the first from replaced by to refused as kind, its message holding what.
bool refused(const fs::path& file, const std::string& text, const std::string& from,
             const std::string& to, terrain::StorageErrorKind kind, const std::string& what) {
    std::string altered = text;
    const std::size_t at = altered.find(from);
    if (at == std::string::npos) {
        std::printf("not in the file: %s\n", from.c_str());
        return false;
    }
    altered.replace(at, from.size(), to);
    writeText(file, altered);
    try {
        (void)terrain::loadSettlementFile(file);
    } catch (const terrain::StorageError& e) {
        std::printf("refused: %s\n", e.what());
        return e.kind() == kind && std::string(e.what()).find(what) != std::string::npos;
    }
    return false;
}

void testRefusals(const fs::path& file) {
    using Kind = terrain::StorageErrorKind;
    const std::string text = readText(file);
    TEST_CHECK(refused(file, text, "\"barrierStop\": 0.7", "\"barrierStop\": 0.8", Kind::Corrupt,
                       "world.config: the copy of the configuration does not match"));
    TEST_CHECK(refused(file, text, "\"civilizedShare\": 0.", "\"civilizedShare\": 0.9",
                       Kind::Invalid,
                       "world.resolved.civilizedShare: does not match civilizedShareBits"));
    TEST_CHECK(refused(file, text, "\"configHash\": \"0x", "\"configHash\": \"0x1", Kind::Invalid,
                       "world.configHash: not \"0x\" and 16 lowercase hex digits"));
    TEST_CHECK(refused(file, text, "\"secretSeedId\": \"\",", "", Kind::Invalid,
                       "world: missing member 'secretSeedId'"));
    TEST_CHECK(refused(file, text, "\"configVersion\": \"1.0\",\n    \"config\"",
                       "\"configVersion\": \"2.0\",\n    \"config\"", Kind::Invalid,
                       "world.configVersion: \"2.0\" is not the configVersion of the copy"));
    TEST_CHECK(refused(file, text, "\"format\": 1", "\"format\": 2", Kind::NewerVersion,
                       "format 2 is newer than this build (1)"));
    TEST_CHECK(refused(file, text, "\"rivers\": \"done\"", "\"rivers\": \"maybe\"", Kind::Invalid,
                       "world.generationState.rivers: \"maybe\" is neither"));
    TEST_CHECK(refused(file, text, "{", "[", Kind::Invalid, "settlement.json:"));
    writeText(file, text);
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    const settlement_test::World w;
    st::SettlementRequest request{"wild", {}, {}, 60};
    const st::Kingdoms k = kingdomsOf(w, w.data, request);
    terrain::SettlementRecord record;
    record.worldSeed = settlement_test::kSeed;
    record.request = request;
    record.resolved = k.preset;
    record.config = w.data.document();
    record.configVersion = w.data.config.version;
    record.kingdomsChecksum = k.checksum();
    record.generationState = {{"terrain", "done"}, {"rivers", "done"}, {"coasts", "pending"},
                              {"civilizations", "done"}, {"hiddenLayer", "pending"}};
    const fs::path dir = storage_test::freshDirectory("terrain_settlement_file_test");
    const fs::path file = dir / terrain::kSettlementFileName;
    terrain::saveSettlementFile(file, record);

    const terrain::SettlementRecord back = terrain::loadSettlementFile(file);
    TEST_CHECK(back.worldSeed == record.worldSeed && back.secretSeedId.empty());
    TEST_CHECK(back.request.preset == "wild" && back.request.minorStates == 60 &&
               !back.request.majorKingdoms && !back.request.civilizedShare);
    TEST_CHECK(back.resolved.preset == "wild" && back.resolved.minorStates == 60 &&
               back.resolved.majorKingdoms == k.preset.majorKingdoms &&
               back.resolved.civilizedShare == k.preset.civilizedShare);
    TEST_CHECK(back.configVersion == "1.0" && back.config == record.config);
    TEST_CHECK(back.kingdomsChecksum == k.checksum());
    TEST_CHECK(back.generationState == record.generationState);
    TEST_CHECK(terrain::settlementConfigHash(back.config) == w.data.fingerprint());

    // The global data files changed since the world was made (barrierStop 0.7 -> 0.9): they give
    // other kingdoms, the copy still gives the same ones.
    const fs::path global = dir / "data";
    fs::copy(TERRAIN_SETTLEMENT_DATA, global, fs::copy_options::recursive);
    const std::string world = readText(global / "world.json");
    const std::size_t at = world.find("\"barrierStop\": 0.7");
    TEST_CHECK(at != std::string::npos);
    writeText(global / "world.json", std::string(world).replace(at + 15, 3, "0.9"));
    const st::SettlementData changed = st::loadSettlementData(global);
    TEST_CHECK(changed.config.expansion.barrierStop == 0.9);
    TEST_CHECK(changed.fingerprint() != w.data.fingerprint());
    const st::Kingdoms other = kingdomsOf(w, changed, back.request);
    TEST_CHECK(other.checksum() != back.kingdomsChecksum);
    const st::SettlementData copy =
        st::settlementDataOf(back.config, terrain::kSettlementFileName);
    TEST_CHECK(copy.fingerprint() == w.data.fingerprint());
    const st::Kingdoms again = kingdomsOf(w, copy, back.request);
    TEST_CHECK(again.checksum() == back.kingdomsChecksum);
    std::printf("kingdoms %016llx, from the changed files %016llx, from the copy %016llx\n",
                static_cast<unsigned long long>(k.checksum()),
                static_cast<unsigned long long>(other.checksum()),
                static_cast<unsigned long long>(again.checksum()));

    testRefusals(file);
    TEST_CHECK(terrain::loadSettlementFile(file).config == record.config);
    fs::remove_all(dir);
    std::printf("in %.2f s\n", settlement_test::secondsSince(start));
    return test::result();
}
