#include "terrain/dev/places_file.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

namespace terrain::dev {

namespace {

// The members of a place this reader knows, in the order they are written.
constexpr std::string_view kPlaceKeys[] = {"name",  "x",       "z",      "heading", "pitch",
                                           "eye",   "eyeMode", "planet", "note"};
constexpr std::string_view kFileKeys[] = {"version", "places"};

template <std::size_t N>
bool known(const std::string_view (&keys)[N], std::string_view key) {
    return std::find(std::begin(keys), std::end(keys), key) != std::end(keys);
}

std::string pathText(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

// A text as a JSON string, quoted and escaped (terrain/json).
std::string jsonQuoted(std::string_view text) {
    return json::write(json::Value::string(std::string(text)), false);
}

// "place 3 (\"k2_paroi\")": what the messages say of a place, from 1.
std::string placeLabel(std::size_t index, std::string_view name) {
    std::string label = "place " + std::to_string(index + 1);
    if (!name.empty()) {
        label += " (" + jsonQuoted(name) + ")";
    }
    return label;
}

// The canonical text of a number of a place (terrain/json: the shortest exact decimal), or
// PlacesError when it has none (more than 2^53 10^+-22: Terrain.Save rounds to the centimeter).
std::string numberText(double value, std::string_view key) {
    try {
        return json::write(json::Value::number(value), false);
    } catch (const json::Error& error) {
        throw PlacesError(jsonQuoted(key) + " cannot be written (" + error.what() + ")");
    }
}

// where: a view, so that a temporary text never seems to hold the member returned.
const json::Value& member(const json::Value& object, std::string_view key, json::Type type,
                          std::string_view where) {
    static constexpr const char* kTypeNames[] = {"null",     "true or false", "a number",
                                                 "a string", "an array",      "an object"};
    const json::Value* value = object.find(key);
    if (value == nullptr) {
        throw PlacesError(std::string(where) + ": " + jsonQuoted(key) + " is missing");
    }
    if (value->type() != type) {
        throw PlacesError(std::string(where) + ": " + jsonQuoted(key) + " must be " +
                          kTypeNames[static_cast<std::size_t>(type)]);
    }
    return *value;
}

double number(const json::Value& object, std::string_view key, std::string_view where) {
    return member(object, key, json::Type::Number, where).asNumber();
}

// The members of object whose keys are not known, in their order.
template <std::size_t N>
json::Value otherMembers(const json::Value& object, const std::string_view (&keys)[N]) {
    json::Value others = json::Value::object();
    for (std::size_t k = 0; k < object.keys().size(); ++k) {
        if (!known(keys, object.keys()[k])) {
            others.set(object.keys()[k], object.items()[k]);
        }
    }
    return others;
}

Place placeOf(const json::Value& value, std::size_t index) {
    const json::Value* name = value.find("name");
    const std::string where =
        placeLabel(index, name != nullptr && name->isString() ? name->asString() : "");
    if (!value.isObject()) {
        throw PlacesError(where + ": a place must be an object");
    }
    Place place;
    place.name = member(value, "name", json::Type::String, where).asString();
    place.x = number(value, "x", where);
    place.z = number(value, "z", where);
    place.heading = number(value, "heading", where);
    place.pitch = number(value, "pitch", where);
    place.eye = number(value, "eye", where);
    const std::string& mode = member(value, "eyeMode", json::Type::String, where).asString();
    if (mode != "altitude" && mode != "ground") {
        throw PlacesError(where + ": \"eyeMode\" must be \"altitude\" or \"ground\", not " +
                          jsonQuoted(mode));
    }
    place.eyeMode = mode == "ground" ? EyeMode::Ground : EyeMode::Altitude;
    place.planet = member(value, "planet", json::Type::Boolean, where).asBoolean();
    if (value.find("note") != nullptr) {
        place.note = member(value, "note", json::Type::String, where).asString();
    }
    place.extra = otherMembers(value, kPlaceKeys);
    try {
        validPlace(place);
    } catch (const PlacesError& error) {
        throw PlacesError(where + ": " + error.what());
    }
    return place;
}

void checkVersion(const json::Value& root) {
    const double version = number(root, "version", "the file");
    if (version != kPlacesFileVersion) {
        throw PlacesError("the file: \"version\" " + numberText(version, "version") +
                          " is not known (this reader reads version " +
                          std::to_string(kPlacesFileVersion) + ")");
    }
}

void requireRange(bool inside, const std::string& message) {
    if (!inside) {
        throw PlacesError(message);
    }
}

// The other members of a place or of the file, written back after the known ones: an object
// whose keys are not known (a known key there would be written twice).
template <std::size_t N>
void requireOthers(const json::Value& others, const std::string_view (&keys)[N]) {
    requireRange(others.isObject() || others.isNull(), "its other members must be an object");
    for (const std::string& key : others.keys()) {
        requireRange(!known(keys, key), jsonQuoted(key) + " is known: not one of its other members");
    }
}

void writePlace(const Place& place, std::string& out) {
    out += "    { \"name\": " + jsonQuoted(place.name) + ", \"x\": " + numberText(place.x, "x") +
           ", \"z\": " + numberText(place.z, "z") + ",\n";
    out += "      \"heading\": " + numberText(place.heading, "heading") +
           ", \"pitch\": " + numberText(place.pitch, "pitch") +
           ", \"eye\": " + numberText(place.eye, "eye") + ", \"eyeMode\": " +
           jsonQuoted(eyeModeName(place.eyeMode)) + ",\n";
    out += std::string("      \"planet\": ") + (place.planet ? "true" : "false");
    if (!place.note.empty()) {
        out += ", \"note\": " + jsonQuoted(place.note);
    }
    for (std::size_t k = 0; k < place.extra.keys().size(); ++k) {
        out += ",\n      " + jsonQuoted(place.extra.keys()[k]) + ": " +
               json::write(place.extra.items()[k], false);
    }
    out += " }";
}

} // namespace

const Place* PlacesFile::find(std::string_view name) const {
    const auto it = std::find_if(places.begin(), places.end(),
                                 [name](const Place& p) { return p.name == name; });
    return it == places.end() ? nullptr : &*it;
}

void PlacesFile::put(Place place) {
    validPlace(place);
    const auto it = std::find_if(places.begin(), places.end(),
                                 [&place](const Place& p) { return p.name == place.name; });
    if (it == places.end()) {
        places.push_back(std::move(place));
        return;
    }
    // What other tools wrote in the place stays, in its order; a member the new place also has
    // takes its new value, the others come after.
    json::Value kept = it->extra.isObject() ? std::move(it->extra) : json::Value::object();
    for (std::size_t k = 0; k < place.extra.keys().size(); ++k) {
        kept.set(place.extra.keys()[k], place.extra.items()[k]);
    }
    place.extra = std::move(kept);
    if (place.note.empty()) {
        place.note = std::move(it->note);
    }
    *it = std::move(place);
}

std::string_view eyeModeName(EyeMode mode) {
    return mode == EyeMode::Ground ? "ground" : "altitude";
}

bool validPlaceName(std::string_view name) {
    if (name.empty() || name.size() > kPlaceNameMaxLength || name.front() == '.') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '.';
    });
}

void validPlace(const Place& place) {
    requireRange(validPlaceName(place.name),
                 "\"name\" must be 1 to " + std::to_string(kPlaceNameMaxLength) +
                     " letters, digits, '_', '-' or '.' (not first)");
    requireRange(std::isfinite(place.x) && std::isfinite(place.z),
                 "\"x\" and \"z\" must be finite");
    requireRange(place.heading >= 0.0 && place.heading < 360.0,
                 "\"heading\" must be in [0, 360) degrees");
    requireRange(place.pitch >= -90.0 && place.pitch <= 90.0,
                 "\"pitch\" must be in [-90, 90] degrees");
    requireRange(std::isfinite(place.eye), "\"eye\" must be finite");
    requireRange(place.eyeMode != EyeMode::Ground || place.eye >= 0.0,
                 "\"eye\" must be >= 0 above the ground (eyeMode \"ground\")");
}

PlacesFile parsePlaces(std::string_view text, std::string_view source) {
    json::Value root;
    try {
        root = json::parse(text, source);
    } catch (const json::Error& error) {
        throw PlacesError(error.what()); // "<source>:<line>:<column>: <reason>"
    }
    try {
        if (!root.isObject()) {
            throw PlacesError("the file must hold an object");
        }
        checkVersion(root);
        PlacesFile file;
        const json::Value& places = member(root, "places", json::Type::Array, "the file");
        for (std::size_t i = 0; i < places.size(); ++i) {
            Place place = placeOf(places.items()[i], i);
            if (file.find(place.name) != nullptr) {
                throw PlacesError(placeLabel(i, place.name) + ": another place has this name");
            }
            file.places.push_back(std::move(place));
        }
        file.extra = otherMembers(root, kFileKeys);
        return file;
    } catch (const PlacesError& error) {
        throw PlacesError(std::string(source) + ": " + error.what());
    }
}

std::string writePlaces(const PlacesFile& file) {
    std::string out = "{ \"version\": " + std::to_string(kPlacesFileVersion) + ",\n";
    try {
        requireOthers(file.extra, kFileKeys);
    } catch (const PlacesError& error) {
        throw PlacesError(std::string("the file: ") + error.what());
    }
    for (std::size_t k = 0; k < file.extra.keys().size(); ++k) {
        out += "  " + jsonQuoted(file.extra.keys()[k]) + ": " +
               json::write(file.extra.items()[k], false) + ",\n";
    }
    out += "  \"places\": [";
    for (std::size_t i = 0; i < file.places.size(); ++i) {
        out += i == 0 ? "\n" : ",\n";
        try {
            validPlace(file.places[i]);
            requireOthers(file.places[i].extra, kPlaceKeys);
            writePlace(file.places[i], out);
        } catch (const PlacesError& error) {
            throw PlacesError(placeLabel(i, file.places[i].name) + ": " + error.what());
        }
    }
    out += " ] }\n";
    return out;
}

PlacesFile loadPlaces(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw PlacesError(pathText(file) + ": cannot be read");
    }
    std::ostringstream text;
    text << in.rdbuf();
    if (in.bad()) {
        throw PlacesError(pathText(file) + ": cannot be read");
    }
    return parsePlaces(text.str(), pathText(file));
}

void savePlaces(const std::filesystem::path& file, const PlacesFile& places) {
    const std::string text = writePlaces(places);
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, error);
            throw PlacesError(pathText(temporary) + ": cannot be written");
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        const std::string why = error.message();
        std::filesystem::remove(temporary, error);
        throw PlacesError(pathText(file) + ": cannot be replaced (" + why + ")");
    }
}

std::vector<Place> matchPlaces(const PlacesFile& file, std::span<const std::string> patterns) {
    std::vector<Place> out;
    const auto add = [&out](const Place& place) {
        if (std::none_of(out.begin(), out.end(),
                         [&place](const Place& p) { return p.name == place.name; })) {
            out.push_back(place);
        }
    };
    for (const std::string& pattern : patterns) {
        const bool prefix = !pattern.empty() && pattern.back() == '*';
        const std::string_view stem(pattern.data(), pattern.size() - (prefix ? 1 : 0));
        bool any = false;
        for (const Place& place : file.places) {
            if (prefix ? place.name.starts_with(stem) : place.name == stem) {
                add(place);
                any = true;
            }
        }
        if (!any) {
            throw PlacesError("no place named " + jsonQuoted(pattern));
        }
    }
    return out;
}

} // namespace terrain::dev
