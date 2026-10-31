// The peoples of the settlement (generation/settlement/people.hpp, suitability.hpp): the 13 files
// of the repository read whole, the preferences of a people merged over its base terrain model,
// the asymmetric relations, every invalid file refused with a clear message, and raceSuitability:
// its theoretical maximum, 0 beyond a hard limit, on the default world where each people likes
// the land its file describes, the same bits whatever the threads.

#include "settlement_world_support.hpp"
#include "terrain/generation/settlement/people.hpp"
#include "terrain/generation/settlement/suitability.hpp"

#include "test_assert.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace st = terrain::settlement;
namespace fs = std::filesystem;

namespace {

const fs::path kData = TERRAIN_SETTLEMENT_DATA;
const fs::path kScratch = TERRAIN_TEST_SCRATCH;

bool near(double a, double b) {
    return std::abs(a - b) < 1e-12;
}

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

fs::path freshCopy() {
    fs::remove_all(kScratch);
    fs::create_directories(kScratch);
    fs::copy(kData, kScratch, fs::copy_options::recursive);
    return kScratch / "peoples";
}

// Replaces the first from by to in a file of a fresh copy of the data.
void edit(std::string_view file, std::string_view from, std::string_view to) {
    const fs::path path = freshCopy() / file;
    std::string text = readText(path);
    const std::size_t at = text.find(from);
    TEST_CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        text.replace(at, from.size(), to);
    }
    writeText(path, text);
}

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

const st::People& peopleOf(const st::SettlementData& data, std::string_view id) {
    return data.peoples[static_cast<std::size_t>(data.people(id))];
}

void testRepositoryPeoples(const st::SettlementData& data) {
    const char* ids[] = {"dragonides", "elfes",       "geants",       "gnomes",  "gobelins",
                         "halfelins",  "hobgobelins", "hommes_betes", "humains", "kobolds",
                         "nains",      "orcs",        "tieflings"};
    TEST_CHECK(data.peoples.size() == 13);
    for (std::size_t k = 0; k < data.peoples.size() && k < 13; ++k) {
        TEST_CHECK(data.peoples[k].id == ids[k]);
        TEST_CHECK(!data.peoples[k].government.empty() && data.peoples[k].syllables.size() >= 2);
    }
    TEST_CHECK(data.people("trolls") == -1);
    const st::People& nains = peopleOf(data, "nains");
    const st::People& orcs = peopleOf(data, "orcs");
    // Asymmetric relations; absent is 0; itself is selfRelation (+1 by default).
    TEST_CHECK(nains.relationTo("orcs") == -0.9 && orcs.relationTo("nains") == -0.4);
    TEST_CHECK(peopleOf(data, "tieflings").relationTo("nains") == 0.0);
    TEST_CHECK(orcs.relationTo("orcs") == 0.4 && peopleOf(data, "halfelins").relationTo("halfelins") == 1.0);
    TEST_CHECK(nains.titleOf("clan_monarchy") == "Roi sous la Montagne" && nains.titleOf("x").empty());
    TEST_CHECK(nains.underground == st::Underground::Partial && nains.preferredDepth == 0.5);
    TEST_CHECK(peopleOf(data, "kobolds").underground == st::Underground::Mostly &&
               !peopleOf(data, "kobolds").needsSurfaceAccess);
    TEST_CHECK(nains.traits[2].min == 0.5 && nains.traits[2].max == 1.0); // greed
    // The preferences of the dwarves: the roughness of their file over the mountain's.
    const auto rough = static_cast<std::size_t>(st::Criterion::Roughness);
    const auto temperature = static_cast<std::size_t>(st::Criterion::Temperature);
    TEST_CHECK(nains.preferences.has[rough] && nains.preferences.range[rough].min == 0.3);
    TEST_CHECK(nains.preferences.has[temperature] &&
               nains.preferences.range[temperature].hardMax == 32.0);
    // The copy of a world gives the same peoples, and the same fingerprint.
    const st::SettlementData back = st::settlementDataOf(data.document(), "copy");
    TEST_CHECK(back.peoples.size() == 13 && back.fingerprint() == data.fingerprint());
    TEST_CHECK(back.peoples[10].preferences == nains.preferences);
}

// A criterion of a file adjusts the trapezoid of the base model field by field; the criteria it
// does not name stay the model's.
void testMerge(const st::SettlementData& data) {
    const terrain::json::Value document = terrain::json::parse(
        "{ \"id\": \"sylvains\", \"type\": \"people\", \"majorKingdomShare\": 1,"
        "  \"minorStateShare\": 0, \"baseTerrainModel\": \"forest\","
        "  \"physicalPreferences\": { \"temperature\": { \"idealMax\": 20, \"max\": 25 },"
        "                             \"altitude\": { \"min\": 0, \"ideal\": 500, \"max\": 2000 } },"
        "  \"physicalWeights\": { \"altitude\": 2 },"
        "  \"government\": [ \"grove\" ], \"names\": { \"syllables\": [ \"sy\", \"la\" ] } }");
    const st::People p = st::readPeople(document, "sylvains.json", data.config);
    const st::Preferences& forest = data.config.model("forest")->preferences;
    const auto t = static_cast<std::size_t>(st::Criterion::Temperature);
    const auto h = static_cast<std::size_t>(st::Criterion::Humidity);
    const auto a = static_cast<std::size_t>(st::Criterion::Altitude);
    TEST_CHECK(p.preferences.range[t].min == forest.range[t].min &&
               p.preferences.range[t].idealMin == forest.range[t].idealMin);
    TEST_CHECK(p.preferences.range[t].idealMax == 20.0 && p.preferences.range[t].max == 25.0);
    TEST_CHECK(p.preferences.has[h] && p.preferences.range[h] == forest.range[h]);
    TEST_CHECK(p.preferences.has[a] && p.preferences.range[a].idealMin == 500.0 &&
               p.preferences.range[a].idealMax == 500.0 && p.preferences.weight[a] == 2.0);
    TEST_CHECK(p.selfRelation == 1.0 && p.habitabilityWeight == 1.0 && p.prana == 0.0);
}

void testInvalidPeoples() {
    edit("nains.json", "\"baseTerrainModel\": \"mountain\"", "\"baseTerrainModel\": \"volcano\"");
    TEST_CHECK(refused({"peoples/nains.json", "baseTerrainModel", "unknown terrain model 'volcano'"}));
    edit("nains.json", "\"orcs\": -0.9", "\"trolls\": -0.9");
    TEST_CHECK(refused({"peoples/nains.json", "relations.trolls", "unknown people"}));
    edit("nains.json", "\"orcs\": -0.9", "\"nains\": -0.9");
    TEST_CHECK(refused({"peoples/nains.json", "relations.nains", "selfRelation"}));
    edit("orcs.json", "\"majorKingdomShare\": 8", "\"majorKingdomShare\": -8");
    TEST_CHECK(refused({"peoples/orcs.json", "majorKingdomShare", "out of"}));
    edit("elfes.json", "\"prana\": 1.0,", "\"music\": 1.0,");
    TEST_CHECK(refused({"peoples/elfes.json", "needs.music", "unknown need"}));
    edit("gnomes.json", "\"government\": [ \"guild_council\", \"tinker_republic\" ]",
         "\"government\": [ ]");
    TEST_CHECK(refused({"peoples/gnomes.json", "government", "at least one government"}));
    edit("geants.json", "\"id\": \"geants\"", "\"id\": \"nains\"");
    TEST_CHECK(refused({"duplicate people 'nains'", "geants.json", "nains.json"}));
    edit("kobolds.json", "\"level\": \"mostly\"", "\"level\": \"deep\"");
    TEST_CHECK(refused({"peoples/kobolds.json", "underground.level", "none, partial, mostly or full"}));
    edit("orcs.json", "\"piety\"", "\"cunning\"");
    TEST_CHECK(refused({"peoples/orcs.json", "rulerTraits.cunning", "unknown trait"}));
    edit("humains.json", "\"deepOcean\"", "\"darkness\"");
    TEST_CHECK(refused({"peoples/humains.json", "fears.darkness", "unknown key"}));
    edit("tieflings.json", "\"type\": \"people\"", "\"type\": \"profession\"");
    TEST_CHECK(refused({"peoples/tieflings.json", "type", "expected \"people\""}));
    edit("halfelins.json", "\"humidity\": { \"min\": 0.25, \"idealMin\": 0.4",
         "\"humidity\": { \"min\": 0.25, \"idealMin\": 0.2");
    TEST_CHECK(refused({"peoples/halfelins.json", "physicalPreferences.humidity", "min <= idealMin"}));
    edit("dragonides.json", "\"dangerSensitivity\"", "\"dangerSensitivty\"");
    TEST_CHECK(refused({"peoples/dragonides.json", "dangerSensitivty", "unknown key"}));
    fs::remove_all(kScratch);
}

void testSuitabilityFormula(const st::SettlementData& data) {
    const st::SuitabilityRule& rule = data.config.suitability;
    const st::People& nains = peopleOf(data, "nains");
    TEST_CHECK(near(st::suitabilityMax(nains, rule), 0.8 + 1.4 + 0.4 + 0.5 + rule.noiseAmplitude));
    st::SuitabilityTerms best{1.0, 1.0, 1.0, 1.0, 0.0, 1.0};
    TEST_CHECK(near(st::suitabilityOf(nains, rule, best), 1.0));
    st::SuitabilityTerms t = best;
    t.danger = 1.0;
    TEST_CHECK(near(st::suitabilityOf(nains, rule, t),
                    1.0 - nains.dangerSensitivity / st::suitabilityMax(nains, rule)));
    t.physical = 0.0; // beyond a hard limit: never, whatever the rest
    TEST_CHECK(st::suitabilityOf(nains, rule, t) == 0.0);
    // A people that dislikes the Prana: the most of it lowers its score.
    const st::People& tieflings = peopleOf(data, "tieflings");
    TEST_CHECK(tieflings.prana < 0.0);
    t = best;
    t.prana = 0.0;
    TEST_CHECK(near(st::suitabilityOf(tieflings, rule, t), 1.0));
    TEST_CHECK(st::suitabilityOf(tieflings, rule, best) < 1.0);
    const st::SuitabilityTerms worst{0.0, 0.01, 1.0, 0.0, 1.0, -1.0};
    TEST_CHECK(st::suitabilityOf(tieflings, rule, worst) == 0.0);
}

// The mean suitability of a people over the land cells of a terrain model.
double meanOn(const st::SettlementMaps& m, const st::SuitabilityMaps& s, int people, int model) {
    double sum = 0.0;
    double n = 0.0;
    const auto& values = s.byPeople[static_cast<std::size_t>(people)].values();
    for (std::size_t k = 0; k < values.size(); ++k) {
        if (m.terrainModel.values()[k] == model) {
            sum += values[k];
            n += 1.0;
        }
    }
    return n > 0.0 ? sum / n / 255.0 : 0.0;
}

int modelIndex(const st::SettlementConfig& c, std::string_view id) {
    return static_cast<int>(c.model(id) - c.terrainModels.data());
}

void testWorldSuitability(const settlement_test::World& w, const st::SettlementMaps& m) {
    const auto start = std::chrono::steady_clock::now();
    const st::SuitabilityMaps s = st::computeSuitability(w.level, m, w.data, settlement_test::kSeed, 0);
    std::printf("suitability of %zu peoples in %.2f s, %.1f MiB\n", s.byPeople.size(),
                settlement_test::secondsSince(start),
                static_cast<double>(s.memoryBytes()) / 1048576.0);
    const st::SettlementConfig& c = w.data.config;
    for (std::size_t k = 0; k < w.data.peoples.size(); ++k) {
        double sum = 0.0;
        double good = 0.0;
        for (const std::uint8_t v : s.byPeople[k].values()) {
            sum += v;
            good += v >= 128 ? 1.0 : 0.0;
        }
        const double n = static_cast<double>(s.byPeople[k].values().size());
        std::printf("  %-12s mean %.3f, at least 0.5 on %5.1f %% of the land\n",
                    w.data.peoples[k].id.c_str(), sum / n / 255.0, 100.0 * good / n);
        TEST_CHECK(good > 0.0); // every people has land it likes
    }
    const int nains = w.data.people("nains");
    const int elfes = w.data.people("elfes");
    const int humains = w.data.people("humains");
    const int mountain = modelIndex(c, "mountain");
    const int plains = modelIndex(c, "plains");
    const int forest = modelIndex(c, "forest");
    const int desert = modelIndex(c, "desert");
    const int glacier = modelIndex(c, "glacier");
    TEST_CHECK(meanOn(m, s, nains, mountain) > meanOn(m, s, nains, plains));
    TEST_CHECK(meanOn(m, s, elfes, forest) > meanOn(m, s, elfes, desert));
    TEST_CHECK(meanOn(m, s, humains, plains) > meanOn(m, s, humains, glacier) + 0.2);
    TEST_CHECK(meanOn(m, s, humains, plains) > meanOn(m, s, nains, plains));
    // Beyond the hard limit of the humans (-22 degrees): 0.
    std::size_t frozen = 0;
    m.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        if (w.level.temperature(cell) < -22.5f) {
            ++frozen;
            TEST_CHECK(s.byPeople[static_cast<std::size_t>(humains)].values()[slot] == 0);
        }
    });
    TEST_CHECK(frozen > 0);
    // The same bits whatever the threads; another seed, another noise.
    const st::SuitabilityMaps one = st::computeSuitability(w.level, m, w.data, settlement_test::kSeed, 1);
    const st::SuitabilityMaps three = st::computeSuitability(w.level, m, w.data, settlement_test::kSeed, 3);
    TEST_CHECK(one.checksum() == s.checksum() && three.checksum() == s.checksum());
    const st::SuitabilityMaps other =
        st::computeSuitability(w.level, m, w.data, settlement_test::kSeed + 1, 0);
    TEST_CHECK(other.checksum() != s.checksum());
}

} // namespace

int main() {
    const st::SettlementData data = st::loadSettlementData(kData);
    testRepositoryPeoples(data);
    testMerge(data);
    testInvalidPeoples();
    testSuitabilityFormula(data);
    const settlement_test::World w;
    const st::SettlementMaps m =
        st::computeSettlementMaps(w.level, &w.field, w.data, settlement_test::kSeed, 0);
    testWorldSuitability(w, m);
    return test::result();
}
