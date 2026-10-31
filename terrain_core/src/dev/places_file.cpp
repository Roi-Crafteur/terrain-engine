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

// "place 3 (\"k2_paroi\")": what the messages say of a place, from 1.
std::string placeLabel(std::size_t index, std::string_view name) {
    std::string label = "place " + std::to_string(index + 1);
    if (!name.empty()) {
        label += " (" + quoteJson(name) + ")";
    }
    return label;
}

// where: a view, so that a temporary text never seems to hold the member returned.
const JsonValue& member(const JsonValue& object, std::string_view key, JsonKind kind,
                        std::string_view where) {
    static constexpr const char* kKindNames[] = {"null",   "true or false", "a number",
                                                 "a string", "an array",     "an object"};
    const JsonValue* value = object.find(key);
    if (value == nullptr) {
        throw PlacesError(std::string(where) + ": " + quoteJson(key) + " is missing");
    }
    if (value->kind != kind) {
        throw PlacesError(std::string(where) + ": " + quoteJson(key) + " must be " +
                          kKindNames[static_cast<std::size_t>(kind)]);
    }
    return *value;
}

double number(const JsonValue& object, std::string_view key, std::string_view where) {
    return member(object, key, JsonKind::Number, where).number;
}

Place placeOf(const JsonValue& value, std::size_t index) {
    const JsonValue* name = value.find("name");
    const std::string where =
        placeLabel(index, name != nullptr && name->kind == JsonKind::String ? name->text : "");
    if (value.kind != JsonKind::Object) {
        throw PlacesError(where + ": a place must be an object");
    }
    Place place;
    place.name = member(value, "name", JsonKind::String, where).text;
    place.x = number(value, "x", where);
    place.z = number(value, "z", where);
    place.heading = number(value, "heading", where);
    place.pitch = number(value, "pitch", where);
    place.eye = number(value, "eye", where);
    const std::string& mode = member(value, "eyeMode", JsonKind::String, where).text;
    if (mode != "altitude" && mode != "ground") {
        throw PlacesError(where + ": \"eyeMode\" must be \"altitude\" or \"ground\", not " +
                          quoteJson(mode));
    }
    place.eyeMode = mode == "ground" ? EyeMode::Ground : EyeMode::Altitude;
    place.planet = member(value, "planet", JsonKind::Bool, where).boolean;
    if (value.find("note") != nullptr) {
        place.note = member(value, "note", JsonKind::String, where).text;
    }
    for (const JsonMember& m : value.members) {
        if (!known(kPlaceKeys, m.key)) {
            place.extra.push_back(m);
        }
    }
    try {
        validPlace(place);
    } catch (const PlacesError& error) {
        throw PlacesError(where + ": " + error.what());
    }
    return place;
}

void checkVersion(const JsonValue& root) {
    const double version = number(root, "version", "the file");
    if (version != kPlacesFileVersion) {
        throw PlacesError("the file: \"version\" " + formatJsonNumber(version) +
                          " is not known (this reader reads version " +
                          std::to_string(kPlacesFileVersion) + ")");
    }
}

void requireRange(bool inside, const std::string& message) {
    if (!inside) {
        throw PlacesError(message);
    }
}

void writePlace(const Place& place, std::string& out) {
    out += "    { \"name\": " + quoteJson(place.name) + ", \"x\": " + formatJsonNumber(place.x) +
           ", \"z\": " + formatJsonNumber(place.z) + ",\n";
    out += "      \"heading\": " + formatJsonNumber(place.heading) +
           ", \"pitch\": " + formatJsonNumber(place.pitch) +
           ", \"eye\": " + formatJsonNumber(place.eye) + ", \"eyeMode\": " +
           quoteJson(eyeModeName(place.eyeMode)) + ",\n";
    out += std::string("      \"planet\": ") + (place.planet ? "true" : "false");
    if (!place.note.empty()) {
        out += ", \"note\": " + quoteJson(place.note);
    }
    for (const JsonMember& m : place.extra) {
        out += ",\n      " + quoteJson(m.key) + ": " + writeJson(m.value);
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
    std::vector<JsonMember> kept = std::move(it->extra);
    for (JsonMember& member : place.extra) {
        const auto same = std::find_if(kept.begin(), kept.end(), [&member](const JsonMember& m) {
            return m.key == member.key;
        });
        if (same != kept.end()) {
            same->value = std::move(member.value);
        } else {
            kept.push_back(std::move(member));
        }
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

PlacesFile parsePlaces(std::string_view text) {
    JsonValue root;
    try {
        root = parseJson(text);
    } catch (const JsonError& error) {
        throw PlacesError(error.what());
    }
    if (root.kind != JsonKind::Object) {
        throw PlacesError("the file must hold an object");
    }
    checkVersion(root);
    PlacesFile file;
    const JsonValue& places = member(root, "places", JsonKind::Array, "the file");
    for (std::size_t i = 0; i < places.items.size(); ++i) {
        Place place = placeOf(places.items[i], i);
        if (file.find(place.name) != nullptr) {
            throw PlacesError(placeLabel(i, place.name) + ": another place has this name");
        }
        file.places.push_back(std::move(place));
    }
    for (const JsonMember& m : root.members) {
        if (!known(kFileKeys, m.key)) {
            file.extra.push_back(m);
        }
    }
    return file;
}

std::string writePlaces(const PlacesFile& file) {
    std::string out = "{ \"version\": " + std::to_string(kPlacesFileVersion) + ",\n";
    for (const JsonMember& m : file.extra) {
        out += "  " + quoteJson(m.key) + ": " + writeJson(m.value) + ",\n";
    }
    out += "  \"places\": [";
    for (std::size_t i = 0; i < file.places.size(); ++i) {
        try {
            validPlace(file.places[i]);
        } catch (const PlacesError& error) {
            throw PlacesError(placeLabel(i, file.places[i].name) + ": " + error.what());
        }
        out += i == 0 ? "\n" : ",\n";
        writePlace(file.places[i], out);
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
    try {
        return parsePlaces(text.str());
    } catch (const PlacesError& error) {
        throw PlacesError(pathText(file) + ": " + error.what());
    }
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
            throw PlacesError("no place named " + quoteJson(pattern));
        }
    }
    return out;
}

} // namespace terrain::dev
