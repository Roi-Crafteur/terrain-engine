// The places of the dev map on the places file of the tools of agent 36
// (Bridge/map_places_file.hpp): a copy of data/dev/places.json read in its order, a place added
// and read back, a place replaced keeping the members another tool wrote, a missing file created
// by an addition, a broken file refused with its line and column.
// Usage: unreal_map_places_file_test <data/dev/places.json> <scratch folder>
#include "map_places_file.hpp"

#include "test_assert.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace terrain;
using namespace terrain::unreal;

namespace {

void write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

dev::Place newPlace() {
    dev::Place p;
    p.name = "carte_test";
    p.x = -2699.0;
    p.z = -1697.0;
    p.heading = 40.0;
    p.pitch = -5.0;
    p.eye = 1.8;
    p.eyeMode = dev::EyeMode::Ground;
    p.note = "la mer au nord-est de la plage de l'origine";
    return p;
}

void testRepositoryCopy(const std::filesystem::path& repository, const std::filesystem::path& dir) {
    const std::filesystem::path copy = dir / "places.json";
    std::filesystem::copy_file(repository, copy, std::filesystem::copy_options::overwrite_existing);
    const PlacesFileSource source(copy);
    const std::vector<dev::Place> places = source.places();
    const dev::PlacesFile file = dev::loadPlaces(repository);
    TEST_CHECK(places.size() == 20 && places == file.places && source.error().empty());
    TEST_CHECK(places.front().name == "chute388_bassin" && places[7].name == "plage_origine");

    PlacesFileSource writer(copy);
    TEST_CHECK(writer.add(newPlace()) && writer.error().empty());
    const std::vector<dev::Place> after = writer.places();
    TEST_CHECK(after.size() == 21 && after.back() == newPlace());
    TEST_CHECK(std::vector<dev::Place>(after.begin(), after.end() - 1) == file.places);
}

void testReplaceKeepsOthers(const std::filesystem::path& dir) {
    const std::filesystem::path file = dir / "others.json";
    write(file, "{ \"version\": 1, \"by\": \"agent 37\",\n  \"places\": [\n"
                "    { \"name\": \"carte_test\", \"x\": 1, \"z\": 2, \"heading\": 3, \"pitch\": 4,"
                " \"eye\": 5, \"eyeMode\": \"altitude\", \"planet\": false, \"note\": \"gardee\","
                " \"tag\": {\"a\": [1, 2]} } ] }\n");
    PlacesFileSource source(file);
    dev::Place moved = newPlace();
    moved.note.clear(); // no note given: the note stays
    TEST_CHECK(source.add(moved));
    const dev::PlacesFile back = dev::loadPlaces(file);
    TEST_CHECK(back.places.size() == 1 && back.places[0].heading == 40.0);
    TEST_CHECK(back.places[0].note == "gardee");
    TEST_CHECK(back.places[0].extra.find("tag") != nullptr);
    TEST_CHECK(back.extra.find("by") != nullptr);
}

void testMissingAndBroken(const std::filesystem::path& dir) {
    const std::filesystem::path missing = dir / "missing.json";
    std::filesystem::remove(missing);
    PlacesFileSource none(missing);
    TEST_CHECK(none.places().empty() && !none.error().empty());
    TEST_CHECK(none.add(newPlace()) && none.places().size() == 1);

    const std::filesystem::path broken = dir / "broken.json";
    write(broken, "{ \"version\": 1,\n  \"places\": [ }\n");
    PlacesFileSource bad(broken);
    TEST_CHECK(bad.places().empty());
    TEST_CHECK(bad.error().find("broken.json:2:") != std::string::npos);
    TEST_CHECK(!bad.add(newPlace()) && !bad.error().empty());
    dev::Place unnamed = newPlace();
    unnamed.name = "deux mots";
    PlacesFileSource good(missing);
    TEST_CHECK(!good.add(unnamed) && !good.error().empty());
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }
    const std::filesystem::path dir = argv[2];
    std::filesystem::create_directories(dir);
    testRepositoryCopy(argv[1], dir);
    testReplaceKeepsOthers(dir);
    testMissingAndBroken(dir);
    return test::result();
}
