// The places of the developers (terrain_core dev/places_file.hpp), the common format of the
// agents 35, 36 and 37: the file of the repository, the exact round trip, the clear refusals,
// the members other readers add, the atomic writes and the patterns of Terrain.Views.
//
// dev_places_file_test <data/dev/places.json>
#include "test_assert.hpp"

#include "terrain/dev/places_file.hpp"

#include <bit>
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
    TEST_CHECK(file.places.size() == 1 && file.extra.empty());
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
    file.places.push_back(placeAt("a", 0.1 + 0.2, -1.0 / 3.0));
    file.places.push_back(placeAt("b.c-d_9", -30015487.999999996, 17498111.5));
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
}

void testExtraMembers() {
    std::string text(kExample);
    text.insert(text.find("  \"places\""), "  \"map\": {\"zoom\": 3},\n");
    text.insert(text.find(" } ] }"), ",\n      \"checks\": [\"water\", 180.03]");
    const PlacesFile file = parsePlaces(text);
    TEST_CHECK(file.extra.size() == 1 && file.extra[0].key == "map");
    TEST_CHECK(file.places[0].extra.size() == 1 && file.places[0].extra[0].key == "checks");
    TEST_CHECK(file.places[0].extra[0].value.items.size() == 2);
    // Kept, written back where they were, in the canonical layout.
    TEST_CHECK(writePlaces(file) == text);
}

void testRefusals() {
    TEST_CHECK(refused("{\"version\": 1, \"places\": [}", "line 1, column 27: expected a value"));
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
        threw = std::string_view(error.what()).find("bad.json: line 2, column 13") !=
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

// The places of the repository: valid, canonical (Terrain.Save rewrites it without noise), and
// holding the views the agents check.
void testRepositoryFile(const char* path) {
    const PlacesFile file = terrain::dev::loadPlaces(path);
    TEST_CHECK(writePlaces(file) == fileText(path));
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
}

} // namespace

int main(int argc, char** argv) {
    testExample();
    testRoundTrip();
    testExtraMembers();
    testRefusals();
    testPut();
    testSaveAndLoad();
    testMatch();
    if (argc < 2) {
        std::fprintf(stderr, "usage: dev_places_file_test <data/dev/places.json>\n");
        return 1;
    }
    testRepositoryFile(argv[1]);
    return test::result();
}
