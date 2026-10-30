// The data files of the settlement (generation/settlement/settlement_data.hpp): the files of the
// repository read whole (the peoples are tested in settlement_peoples_test), the copy of a world
// read back the same, and every invalid file refused with a clear message (the file, the path in
// it, the reason).

#include "terrain/generation/settlement/settlement_data.hpp"

#include "test_assert.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>

namespace st = terrain::settlement;
namespace fs = std::filesystem;

namespace {

const fs::path kData = TERRAIN_SETTLEMENT_DATA;
const fs::path kScratch = TERRAIN_TEST_SCRATCH;

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

// A fresh copy of the data of the repository.
fs::path freshCopy() {
    fs::remove_all(kScratch);
    fs::create_directories(kScratch);
    fs::copy(kData, kScratch, fs::copy_options::recursive);
    return kScratch;
}

// Replaces the first from by to in a file of the copy.
void edit(const fs::path& file, std::string_view from, std::string_view to) {
    std::string text = readText(file);
    const std::size_t at = text.find(from);
    TEST_CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        text.replace(at, from.size(), to);
    }
    writeText(file, text);
}

// The data of the copy is refused with a message holding every part of expected.
bool refused(std::initializer_list<std::string_view> expected) {
    std::string message;
    try {
        static_cast<void>(st::loadSettlementData(kScratch));
    } catch (const terrain::json::Error& e) {
        message = e.what();
    }
    bool ok = !message.empty();
    for (const std::string_view part : expected) {
        ok = ok && message.find(part) != std::string::npos;
    }
    std::printf("%s: %s\n", ok ? "refused as expected" : "UNEXPECTED", message.c_str());
    return ok;
}

void testRepositoryData() {
    const st::SettlementData data = st::loadSettlementData(kData);
    TEST_CHECK(data.resources.size() == 6);
    const char* ids[] = {"bois", "cristaux_de_mana", "metaux_communs", "metaux_precieux",
                         "pierre", "sel"};
    for (std::size_t k = 0; k < data.resources.size(); ++k) {
        TEST_CHECK(data.resources[k].id == ids[k]);
    }
    TEST_CHECK(data.resource("sel") != nullptr && data.resource("or") == nullptr);
    TEST_CHECK(data.resource("cristaux_de_mana")->hidden);
    TEST_CHECK(data.config.terrainModels.size() == 8);
    TEST_CHECK(data.config.model("plains") != nullptr &&
               data.config.model("plains")->expansionCost == 1.0);
    TEST_CHECK(data.config.model("mountain")->expansionCost == 10.0);
    TEST_CHECK(data.config.arable.alluvialBonus == 0.2);
    TEST_CHECK(data.config.habitability.danger == -1.0);
    TEST_CHECK(data.config.mana.craters == 0.8 && data.config.mana.volcanoes == 0.15);
    TEST_CHECK(data.config.geology.arcaneCraters.min == 30 &&
               data.config.geology.arcaneCraters.max == 80);
    TEST_CHECK(data.config.magic.nodes.min == 420 && data.config.magic.nodes.max == 1050);
    TEST_CHECK(data.config.danger.factors.size() == 6);
    // The copy of a world gives the same data, and the same fingerprint.
    const st::SettlementData back = st::settlementDataOf(data.document(), "copy");
    TEST_CHECK(back.fingerprint() == data.fingerprint());
    TEST_CHECK(back.resources.size() == 6 && back.config.terrainModels.size() == 8);
    TEST_CHECK(data.peoples.size() == 13 && back.peoples.size() == 13);
    TEST_CHECK(data.config.suitability.noiseAmplitude == 0.2);
    TEST_CHECK(data.config.presets.size() == 3 && data.config.defaultPreset == "wild");
    TEST_CHECK(data.config.preset("civilized")->majorKingdoms.max == 150.0);
    TEST_CHECK(data.config.expansion.sea == st::SeaCrossing::Straits &&
               data.config.expansion.seaCost == 50.0 && data.config.expansion.straitCells == 2);
}

void testInvalidWorld() {
    const fs::path world = freshCopy() / "world.json";
    edit(world, "\"alluvialBonus\": 0.2,", "\"alluvialBonus\": 0.2,,");
    TEST_CHECK(refused({"world.json:", "expected a key"}));
    freshCopy();
    edit(world, "\"min\": 0, \"idealMin\": 10, \"idealMax\": 22",
         "\"min\": 0, \"idealMin\": 30, \"idealMax\": 22");
    TEST_CHECK(refused({"world.json", "arable.temperature", "idealMin <= idealMax", "line"}));
    freshCopy();
    edit(world, "\"rangeWeight\"", "\"rangeWieght\"");
    TEST_CHECK(refused({"world.json", "roughness.rangeWieght", "unknown key"}));
    freshCopy();
    edit(world, "\"sampleWeight\": 0.8", "\"sampleWeight\": 0.9");
    TEST_CHECK(refused({"world.json", "roughness", "sampleWeight + rangeWeight above 1"}));
    freshCopy();
    edit(world, "\"humidity\": { \"min\": 0.12", "\"slope\": { \"min\": 0.12");
    TEST_CHECK(refused({"terrainModels.plains.physicalPreferences.slope", "unknown criterion"}));
    freshCopy();
    edit(world, "\"alluvialBonus\": 0.2", "\"alluvialBonus\": 2");
    TEST_CHECK(refused({"arable.alluvialBonus", "out of [0, 1]"}));
    freshCopy();
    edit(world, "\"arcaneCraters\": { \"min\": 30, \"max\": 80 }",
         "\"arcaneCraters\": { \"min\": 90, \"max\": 80 }");
    TEST_CHECK(refused({"geology.arcaneCraters", "min is above max"}));
    freshCopy();
    edit(world, "\"configVersion\": \"1.0\",", "");
    TEST_CHECK(refused({"configVersion: missing"}));
    freshCopy();
    edit(world, "\"weight\": 0.45, \"ramps\": { \"roughness\"",
         "\"weight\": \"high\", \"ramps\": { \"roughness\"");
    TEST_CHECK(refused({"danger.factors[0].weight", "expected a number, found a string"}));
}

void testInvalidKingdoms() {
    const fs::path world = freshCopy() / "world.json";
    edit(world, "\"defaultPreset\": \"wild\"", "\"defaultPreset\": \"medieval\"");
    TEST_CHECK(refused({"world.json", "defaultPreset", "unknown preset 'medieval'"}));
    freshCopy();
    edit(world, "\"mode\": \"range\", \"min\": 10, \"max\": 20",
         "\"mode\": \"random\", \"min\": 10, \"max\": 20");
    TEST_CHECK(refused({"worldPresets.wild.majorKingdoms.mode", "\"range\" or \"exact\""}));
    freshCopy();
    edit(world, "\"mode\": \"range\", \"min\": 10, \"max\": 20",
         "\"mode\": \"range\", \"min\": 30, \"max\": 20");
    TEST_CHECK(refused({"worldPresets.wild.majorKingdoms", "min is above max"}));
    freshCopy();
    edit(world, "\"barrierModels\": [ \"mountain\", \"glacier\" ]",
         "\"barrierModels\": [ \"volcano\" ]");
    TEST_CHECK(refused({"expansion.barrierModels", "unknown terrain model 'volcano'"}));
    freshCopy();
    edit(world, "\"sea\": \"straits\"", "\"sea\": \"bridges\"");
    TEST_CHECK(refused({"expansion.sea", "\"straits\" or \"forbidden\""}));
    freshCopy();
    edit(world, "\"preferenceWeight\": 0.5", "\"preferenceWeight\": 0.9");
    TEST_CHECK(refused({"expansion.preferenceWeight", "out of [0, 0.6]"}));
    freshCopy();
    edit(world, "\"amplitude\": 0.5", "\"amplitude\": 0.95");
    TEST_CHECK(refused({"expansion.costNoise.amplitude", "out of [0, 0.9]"}));
    freshCopy();
    edit(world, "\"barrierStop\": 0.7", "\"barrierStop\": 0.05");
    TEST_CHECK(refused({"expansion.barrierStop", "out of [0.1, 1]"}));
}

void testInvalidResources() {
    const fs::path resources = freshCopy() / "resources";
    edit(resources / "sel.json", "\"id\": \"sel\",", "");
    TEST_CHECK(refused({"resources/sel.json", "id: missing"}));
    freshCopy();
    edit(resources / "bois.json", "\"forestCover\"", "\"jungle\"");
    TEST_CHECK(refused({"resources/bois.json", "sources[0].kind", "unknown source jungle"}));
    freshCopy();
    edit(resources / "pierre.json", "\"id\": \"pierre\"", "\"id\": \"bois\"");
    TEST_CHECK(refused({"duplicate resource 'bois'", "bois.json"}));
    freshCopy();
    edit(resources / "metaux_communs.json", "\"threshold\": 0.55", "\"threshold\": -1");
    TEST_CHECK(refused({"metaux_communs.json", "depositNoise.threshold", "out of"}));
}

// A seventh resource is one more file: no code changes.
void testNewResourceFile() {
    const fs::path resources = freshCopy() / "resources";
    writeText(resources / "argile.json",
              "{ \"id\": \"argile\", \"name\": \"Argile\",\n"
              "  \"sources\": [ { \"kind\": \"closedBasin\", \"weight\": 0.5 },\n"
              "               { \"kind\": \"forestCover\", \"weight\": 0.2 } ],\n"
              "  \"depositNoise\": { \"scaleKm\": 120, \"threshold\": 0.5 } }\n");
    const st::SettlementData data = st::loadSettlementData(kScratch);
    TEST_CHECK(data.resources.size() == 7 && data.resources[0].id == "argile");
    TEST_CHECK(data.resource("argile")->sources.size() == 2);
    TEST_CHECK(data.fingerprint() != st::loadSettlementData(kData).fingerprint());
    fs::remove_all(kScratch);
}

} // namespace

int main() {
    testRepositoryData();
    testInvalidWorld();
    testInvalidKingdoms();
    testInvalidResources();
    testNewResourceFile();
    return test::result();
}
