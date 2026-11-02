// The places of the developers (terrain_core dev/places_file.hpp), the common format of the
// agents 35, 36 and 37: the file of the repository, the exact round trip, the clear refusals,
// the members other readers add, the atomic writes and the patterns of Terrain.Views.
//
// dev_places_file_test <data/dev/places.json>
#include "test_assert.hpp"

#include "terrain/dev/places_file.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

using terrain::dev::EyeMode;
using terrain::dev::parsePlaces;
using terrain::dev::Place;
using terrain::dev::PlacesError;
using terrain::dev::PlacesFile;
using terrain::dev::writePlaces;

// The example of the format, as the brief of the agents gives it.
constexpr std::string_view kExample =
    "{ \"version\": 1,\n"
    "  \"places\": [\n"
    "    { \"name\": \"chute388_bassin\", \"x\": -6185104, \"z\": 155924,\n"
    "      \"heading\": 227, \"pitch\": 30, \"eye\": 182.03, \"eyeMode\": \"altitude\",\n"
    "      \"planet\": true, \"note\": \"chute de 388 m vue depuis son bassin\" } ] }\n";

std::string errorOf(std::string_view text) {
    try {
        (void)parsePlaces(text);
    } catch (const PlacesError& error) {
        return error.what();
    }
    return {};
}

bool refused(std::string_view text, std::string_view part) {
    const std::string message = errorOf(text);
    if (message.find(part) == std::string::npos) {
        std::fprintf(stderr, "  -> \"%s\", expected \"%.*s\"\n", message.c_str(),
                     static_cast<int>(part.size()), part.data());
        return false;
    }
    return true;
}

// The example with one member of its place replaced by what (a text "\"key\": value").
std::string withMember(std::string_view key, std::string_view what) {
    std::string text(kExample);
    const std::size_t at = text.find("\"" + std::string(key) + "\": ");
    const std::size_t end = text.find_first_of(",}", text.find(':', at) + 2);
    std::string replaced = text.substr(0, at) + std::string(what);
    if (what.empty() && text[end] == ',') {
        return replaced + text.substr(text.find_first_not_of(" \n", end + 1));
    }
    return replaced + text.substr(end);
}

bool sameBits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

void testExample() {
    const PlacesFile file = parsePlaces(kExample);
    TEST_CHECK(file.places.size() == 1 && file.extra.isObject() && file.extra.size() == 0);
    const Place* p = file.find("chute388_bassin");
    TEST_CHECK(p != nullptr && p == &file.places[0]);
    TEST_CHECK(p->x == -6185104.0 && p->z == 155924.0);
    TEST_CHECK(p->heading == 227.0 && p->pitch == 30.0 && p->eye == 182.03);
    TEST_CHECK(p->eyeMode == EyeMode::Altitude && p->planet);
    TEST_CHECK(p->note == "chute de 388 m vue depuis son bassin");
    TEST_CHECK(file.find("k2") == nullptr);
    // The example is canonical: written back to the same bytes.
    TEST_CHECK(writePlaces(file) == kExample);
}

Place placeAt(std::string name, double x, double z) {
    Place p;
    p.name = std::move(name);
    p.x = x;
    p.z = z;
    p.heading = 359.99;
    p.pitch = -89.5;
    p.eye = 1.8;
    p.eyeMode = EyeMode::Ground;
    p.note = "accents \xC3\xA9\xC3\xA8, \"quotes\" and \\ backslash\nnew line";
    return p;
}

void testRoundTrip() {
    PlacesFile file;
    file.places.push_back(placeAt("a", 0.3, -0.33));
    file.places.push_back(placeAt("b.c-d_9", -30015487.99, 17498111.5));
    file.places.back().note.clear();
    file.places.back().planet = true;
    file.places.back().eyeMode = EyeMode::Altitude;
    file.places.back().eye = -10994.25;
    file.places.back().heading = 0.0;
    const std::string text = writePlaces(file);
    const PlacesFile back = parsePlaces(text);
    TEST_CHECK(back == file);
    for (std::size_t i = 0; i < file.places.size(); ++i) {
        TEST_CHECK(sameBits(back.places[i].x, file.places[i].x));
        TEST_CHECK(sameBits(back.places[i].z, file.places[i].z));
        TEST_CHECK(sameBits(back.places[i].heading, file.places[i].heading));
        TEST_CHECK(sameBits(back.places[i].eye, file.places[i].eye));
    }
    TEST_CHECK(writePlaces(back) == text);
    // An empty file of places.
    TEST_CHECK(writePlaces(PlacesFile{}) == "{ \"version\": 1,\n  \"places\": [ ] }\n");
    TEST_CHECK(parsePlaces(writePlaces(PlacesFile{})) == PlacesFile{});
    // Any layout reads the same; the writer gives the canonical one.
    const std::string compact =
        "{\"places\":[{\"note\":\"chute de 388 m vue depuis son bassin\",\"planet\":true,"
        "\"eyeMode\":\"altitude\",\"eye\":182.03,\"pitch\":30,\"heading\":227,\"z\":155924,"
        "\"x\":-6185104,\"name\":\"chute388_bassin\"}],\"version\":1}";
    TEST_CHECK(parsePlaces(compact) == parsePlaces(kExample));
    TEST_CHECK(writePlaces(parsePlaces(compact)) == kExample);
    // What Terrain.Save writes (centimeters, hundredths of a degree) reads back bit for bit.
    std::uint64_t state = 0x9E3779B97F4A7C15u;
    PlacesFile many;
    for (int i = 0; i < 2000; ++i) {
        state = state * 6364136223846793005u + 1442695040888963407u;
        const double unit = static_cast<double>(state >> 11) / 9007199254740992.0; // [0, 1)
        Place p = placeAt("p" + std::to_string(i), std::round((unit - 0.5) * 6.0e9) / 100.0,
                          std::round((0.5 - unit) * 3.4e9) / 100.0);
        p.heading = std::round(unit * 35999.0) / 100.0;
        p.eye = std::round(unit * 1.0e6) / 100.0;
        many.places.push_back(p);
    }
    const PlacesFile manyBack = parsePlaces(writePlaces(many));
    TEST_CHECK(manyBack == many);
}

// What cannot be written is refused, the place and the member named: a number without an exact
// short decimal, a known key among the other members.
void testWriteRefusals() {
    PlacesFile file;
    file.places.push_back(placeAt("a", 0.1 + 0.2, 0.0));
    std::string message;
    try {
        (void)writePlaces(file);
    } catch (const PlacesError& error) {
        message = error.what();
    }
    TEST_CHECK(message.find("place 1 (\"a\"): \"x\" cannot be written") != std::string::npos);
    file.places[0].x = 0.3;
    file.places[0].extra.set("pitch", terrain::json::Value::number(3.0));
    message.clear();
    try {
        (void)writePlaces(file);
    } catch (const PlacesError& error) {
        message = error.what();
    }
    TEST_CHECK(message.find("place 1 (\"a\"): \"pitch\" is known") != std::string::npos);
}

void testExtraMembers() {
    std::string text(kExample);
    text.insert(text.find("  \"places\""), "  \"map\": {\"zoom\":3},\n");
    text.insert(text.find(" } ] }"), ",\n      \"checks\": [\"water\",180.03]");
    const PlacesFile file = parsePlaces(text);
    TEST_CHECK(file.extra.keys() == std::vector<std::string>{"map"});
    TEST_CHECK(file.places[0].extra.keys() == std::vector<std::string>{"checks"});
    TEST_CHECK(file.places[0].extra.items()[0].size() == 2);
    // Kept, written back where they were, in the canonical layout (compact: terrain/json).
    TEST_CHECK(writePlaces(file) == text);
    std::string spaced(kExample);
    spaced.insert(spaced.find(" } ] }"), ",\n      \"checks\": [ \"water\" , 180.03 ]");
    TEST_CHECK(parsePlaces(spaced).places[0].extra == file.places[0].extra);
}

void testRefusals() {
    TEST_CHECK(refused("{\"version\": 1, \"places\": [}", "<places>:1:27: expected a value"));
    TEST_CHECK(refused(withMember("x", "\"x\": -6185104.12345678901"), "too many significant"));
    TEST_CHECK(refused("[]", "must hold an object"));
    TEST_CHECK(refused("{\"places\": []}", "\"version\" is missing"));
    TEST_CHECK(refused("{\"version\": 2, \"places\": []}", "\"version\" 2 is not known"));
    TEST_CHECK(refused("{\"version\": \"1\", \"places\": []}", "\"version\" must be a number"));
    TEST_CHECK(refused("{\"version\": 1}", "\"places\" is missing"));
    TEST_CHECK(refused("{\"version\": 1, \"places\": {}}", "\"places\" must be an array"));
    TEST_CHECK(refused("{\"version\": 1, \"places\": [3]}", "place 1: a place must be an object"));
    TEST_CHECK(refused(withMember("pitch", ""), "place 1 (\"chute388_bassin\"): \"pitch\" is "
                                                "missing"));
    TEST_CHECK(refused(withMember("x", "\"x\": \"-6185104\""), "\"x\" must be a number"));
    TEST_CHECK(refused(withMember("planet", "\"planet\": 1"), "\"planet\" must be true or false"));
    TEST_CHECK(refused(withMember("note", "\"note\": null"), "\"note\" must be a string"));
    TEST_CHECK(refused(withMember("eyeMode", "\"eyeMode\": \"sol\""),
                       "\"eyeMode\" must be \"altitude\" or \"ground\", not \"sol\""));
    TEST_CHECK(refused(withMember("pitch", "\"pitch\": 90.5"), "\"pitch\" must be in [-90, 90]"));
    TEST_CHECK(refused(withMember("heading", "\"heading\": 360"),
                       "\"heading\" must be in [0, 360)"));
    TEST_CHECK(refused(withMember("heading", "\"heading\": -1"), "[0, 360)"));
    TEST_CHECK(refused(withMember("name", "\"name\": \"\""), "\"name\" must be 1 to 64"));
    TEST_CHECK(refused(withMember("name", "\"name\": \"../up\""), "\"name\" must be"));
    TEST_CHECK(refused(withMember("name", "\"name\": \"a b\""), "\"name\" must be"));
    TEST_CHECK(refused(withMember("eyeMode", "\"eyeMode\": \"ground\"").replace(
                           std::string(kExample).find("182.03"), 6, "-1.000"),
                       "\"eye\" must be >= 0 above the ground"));
    TEST_CHECK(refused(withMember("eye", "\"eye\": NaN"), "expected a value"));
    std::string twice(kExample);
    const std::string place = twice.substr(twice.find("    {"), twice.find(" ] }") -
                                                                     twice.find("    {"));
    twice.insert(twice.find(" ] }"), ",\n" + place);
    TEST_CHECK(refused(twice, "place 2 (\"chute388_bassin\"): another place has this name"));
}

void testPut() {
    PlacesFile file = parsePlaces(kExample);
    Place p = placeAt("plage_origine", -3985.0, -165.0);
    file.put(p);
    TEST_CHECK(file.places.size() == 2 && file.places[1].name == "plage_origine");
    p.eye = 2.5;
    file.put(p);
    TEST_CHECK(file.places.size() == 2 && file.places[1].eye == 2.5);
    p.pitch = 100.0;
    bool threw = false;
    try {
        file.put(p);
    } catch (const PlacesError& error) {
        threw = std::string_view(error.what()).find("pitch") != std::string_view::npos;
    }
    TEST_CHECK(threw && file.places[1].pitch == -89.5);
    file.places[1].heading = 400.0; // invalid places are never written
    threw = false;
    try {
        (void)writePlaces(file);
    } catch (const PlacesError& error) {
        threw = std::string_view(error.what()).find("place 2 (\"plage_origine\")") !=
                std::string_view::npos;
    }
    TEST_CHECK(threw);
}

// Terrain.Save over a place (put): what other tools wrote in it stays, bit for bit and in its
// order, and its note when the new place has none; only the known values change.
void testPutKeepsOtherMembers() {
    const std::string others = ",\n      \"probe\": {\"water\":180.03,\"seen\":[true,null,"
                               "{\"by\":\"agent 37\"}]},\n      \"agent37\": 0.1";
    std::string text(kExample);
    text.insert(text.find(" } ] }"), others);
    PlacesFile file = parsePlaces(text);
    Place saved = *file.find("chute388_bassin");
    saved.extra = terrain::json::Value::object();
    saved.note.clear();
    saved.eye = 190.5;
    file.put(saved);
    std::string expected = text;
    expected.replace(expected.find("182.03"), 6, "190.5");
    TEST_CHECK(writePlaces(file) == expected);

    // A member the new place also has takes its new value where it was; a new one comes after.
    std::string own(kExample);
    own.insert(own.find(" } ] }"), ",\n      \"agent37\": 2,\n      \"added\": true");
    saved.extra = parsePlaces(own).places[0].extra;
    saved.note = "new note";
    file.put(saved);
    expected.replace(expected.find("\"agent37\": 0.1"), 14, "\"agent37\": 2,\n      \"added\": true");
    expected.replace(expected.find("chute de 388 m vue depuis son bassin"), 36, "new note");
    TEST_CHECK(writePlaces(file) == expected);
    TEST_CHECK(file.places.size() == 1);
}

// The file of the agent 37 (measured with its probe terrain_probe): another layout, notes in
// UTF-8. Read the same; written back in the canonical layout, read back to the same places.
void testOtherLayout() {
    constexpr std::string_view kAgent37 =
        "{ \"version\": 1,\n"
        "  \"places\": [\n"
        "    { \"name\": \"chute388_levre\", \"x\": -6185411, \"z\": 156218,\n"
        "      \"heading\": 45.9, \"pitch\": -30, \"eye\": 3, \"eyeMode\": \"ground\",\n"
        "      \"planet\": true, \"note\": \"l\xC3\xA8vre de la chute (eau \xC3\xA0 568,99 m)\" },\n"
        "    { \"name\": \"lisiere_tropicale_savane\", \"x\": -26927500, \"z\": -270336,\n"
        "      \"heading\": 300, \"pitch\": -8, \"eye\": 40, \"eyeMode\": \"ground\",\n"
        "      \"planet\": false, \"note\": \"sans plan\xC3\xA8te seulement\" }\n"
        "  ] }\n";
    const PlacesFile file = parsePlaces(kAgent37);
    TEST_CHECK(file.places.size() == 2);
    const Place* lip = file.find("chute388_levre");
    TEST_CHECK(lip != nullptr && lip->x == -6185411.0 && lip->heading == 45.9 &&
               lip->pitch == -30.0 && lip->eye == 3.0 && lip->eyeMode == EyeMode::Ground &&
               lip->planet && lip->note == "l\xC3\xA8vre de la chute (eau \xC3\xA0 568,99 m)");
    const Place* edge = file.find("lisiere_tropicale_savane");
    TEST_CHECK(edge != nullptr && !edge->planet && edge->pitch == -8.0 && edge->extra.size() == 0);
    TEST_CHECK(parsePlaces(writePlaces(file)) == file);
}

std::string fileText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void testSaveAndLoad() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dev_places_file_test";
    std::filesystem::remove_all(dir);
    const std::filesystem::path file = dir / "sub" / "places.json";
    PlacesFile places = parsePlaces(kExample);
    terrain::dev::savePlaces(file, places); // creates its folders
    TEST_CHECK(fileText(file) == kExample);
    places.put(placeAt("k2_paroi", 416857.0, -11405966.0));
    terrain::dev::savePlaces(file, places); // replaces the file
    TEST_CHECK(terrain::dev::loadPlaces(file) == places);
    TEST_CHECK(!std::filesystem::exists(dir / "sub" / "places.json.tmp"));
    bool threw = false;
    try {
        (void)terrain::dev::loadPlaces(dir / "missing.json");
    } catch (const PlacesError& error) {
        threw = std::string_view(error.what()).find("missing.json: cannot be read") !=
                std::string_view::npos;
    }
    TEST_CHECK(threw);
    std::ofstream(dir / "bad.json") << "{\"version\": 1,\n \"places\": [,]}";
    threw = false;
    try {
        (void)terrain::dev::loadPlaces(dir / "bad.json");
    } catch (const PlacesError& error) {
        threw = std::string_view(error.what()).find("bad.json:2:13: expected a value") !=
                std::string_view::npos;
    }
    TEST_CHECK(threw);
    std::filesystem::remove_all(dir);
}

void testMatch() {
    PlacesFile file;
    for (const char* name : {"chute388_bassin", "chute388_levre", "k2_paroi", "chute388_mer"}) {
        file.places.push_back(placeAt(name, 0.0, 0.0));
    }
    const auto names = [&file](std::vector<std::string> patterns) {
        std::string out;
        for (const Place& p : terrain::dev::matchPlaces(file, patterns)) {
            out += p.name + " ";
        }
        return out;
    };
    TEST_CHECK(names({"k2_paroi", "chute388_mer"}) == "k2_paroi chute388_mer ");
    TEST_CHECK(names({"chute388_*"}) == "chute388_bassin chute388_levre chute388_mer ");
    TEST_CHECK(names({"k2_paroi", "*"}) ==
               "k2_paroi chute388_bassin chute388_levre chute388_mer ");
    bool threw = false;
    try {
        (void)terrain::dev::matchPlaces(file, std::vector<std::string>{"lac*"});
    } catch (const PlacesError& error) {
        threw = std::string_view(error.what()) == "no place named \"lac*\"";
    }
    TEST_CHECK(threw);
}

// The places of the repository: valid, canonical (Terrain.Save rewrites it without noise; LF on
// every platform by .gitattributes, a checkout in CRLF read the same), and holding the views the
// agents check.
void testRepositoryFile(const char* path) {
    const PlacesFile file = terrain::dev::loadPlaces(path);
    std::string text = fileText(path);
    std::erase(text, '\r');
    TEST_CHECK(writePlaces(file) == text);
    for (const char* name : {"chute388_bassin", "chute388_levre", "chute388_mer", "k2_paroi",
                             "falaise_cotiere", "montagnes_mer", "cote_froide", "plage_origine",
                             "fleuve_plaine", "lac_montagne", "lisiere_tropicale_savane"}) {
        if (file.find(name) == nullptr) {
            std::fprintf(stderr, "  %s: no place %s\n", path, name);
            TEST_CHECK(file.find(name) != nullptr);
        }
    }
    const Place* basin = file.find("chute388_bassin");
    TEST_CHECK(basin != nullptr && basin->x == -6185104.0 && basin->eye == 182.03);
    // Measured with the planet file, as Unreal loads it, but the edge that exists only without
    // it (with it, savanna all around).
    for (const Place& place : file.places) {
        const bool withPlanet = place.name != "lisiere_tropicale_savane";
        if (place.planet != withPlanet) {
            std::fprintf(stderr, "  %s: %s planet %d\n", path, place.name.c_str(), place.planet);
            TEST_CHECK(place.planet == withPlanet);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    testExample();
    testRoundTrip();
    testWriteRefusals();
    testExtraMembers();
    testRefusals();
    testPut();
    testPutKeepsOtherMembers();
    testOtherLayout();
    testSaveAndLoad();
    testMatch();
    if (argc < 2) {
        std::fprintf(stderr, "usage: dev_places_file_test <data/dev/places.json>\n");
        return 1;
    }
    testRepositoryFile(argv[1]);
    return test::result();
}
