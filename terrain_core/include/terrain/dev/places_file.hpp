#pragma once

#include "terrain/dev/json_value.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The places of the developers (agents 35, 36 and 37): data/dev/places.json, the views to check
// a change of the world from, the same in the game (Terrain.Goto, Terrain.Views), on the dev map
// and in the tools. A view of the world, never a part of it: nothing here changes a bit of it.
//
//   { "version": 1,
//     "places": [
//       { "name": "chute388_bassin", "x": -6185104, "z": 155924,
//         "heading": 227, "pitch": 30, "eye": 182.03, "eyeMode": "altitude",
//         "planet": true, "note": "chute de 388 m vue depuis son bassin" } ] }
//
// x, z: the point of the core, meters (x east, z south); heading: degrees clockwise from the
// north (-z), in [0, 360) (the yaw of Unreal is heading - 90); pitch: degrees up, in [-90, 90];
// eye: meters, the altitude of the eye in the core (eyeMode "altitude") or its height above the
// ground or the water (eyeMode "ground", >= 0); planet: the view needs the planet file (rivers,
// falls, lakes, the climate of the biomes); note: optional. The members a reader does not know,
// in a place or around the places, are kept as they are and written back after the known ones.
namespace terrain::dev {

inline constexpr int kPlacesFileVersion = 1;
// Where the places of the repository lie, from its root.
inline constexpr const char* kPlacesFilePath = "data/dev/places.json";
// The longest name: names become file names (Terrain.Shot, Terrain.Views).
inline constexpr std::size_t kPlaceNameMaxLength = 64;

enum class EyeMode : std::uint8_t { Altitude, Ground };

struct Place {
    std::string name;
    double x = 0.0;
    double z = 0.0;
    double heading = 0.0;
    double pitch = 0.0;
    double eye = 0.0;
    EyeMode eyeMode = EyeMode::Altitude;
    bool planet = false;
    std::string note;               // empty: none (not written)
    std::vector<JsonMember> extra; // the other members, in their order

    friend bool operator==(const Place&, const Place&) = default;
};

struct PlacesFile {
    std::vector<Place> places;
    std::vector<JsonMember> extra; // the other members of the file, in their order

    // The place of this name, or nullptr.
    [[nodiscard]] const Place* find(std::string_view name) const;
    // Replaces the place of the same name, or adds it at the end. Throws PlacesError for an
    // invalid place (validPlace).
    void put(Place place);

    friend bool operator==(const PlacesFile&, const PlacesFile&) = default;
};

// Why a file of places was refused, a clear message: "places.json: line 4, column 9: expected
// ',' or '}'", "place 3 (\"k2_paroi\"): \"pitch\" must be in [-90, 90] degrees".
class PlacesError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string_view eyeModeName(EyeMode mode);

// A name made of letters, digits, '_', '-' and '.', not starting with '.', of 1 to
// kPlaceNameMaxLength characters.
[[nodiscard]] bool validPlaceName(std::string_view name);
// Throws PlacesError if a value of the place is out of its range (the comment above).
void validPlace(const Place& place);

// The places of a strict JSON text. Throws PlacesError.
[[nodiscard]] PlacesFile parsePlaces(std::string_view text);
// The canonical text of the places (the layout of the comment above, one place per three lines
// or more, a final newline): parsePlaces reads it back to the same places, bit for bit, and
// writePlaces(parsePlaces(text)) == text for a canonical text. Throws PlacesError for an invalid
// place.
[[nodiscard]] std::string writePlaces(const PlacesFile& file);

// parsePlaces of a file, its path in the messages. Throws PlacesError (also when it cannot be
// read).
[[nodiscard]] PlacesFile loadPlaces(const std::filesystem::path& file);
// Writes the places to file, whole or not at all: to <file>.tmp, then renamed over file (its
// folder created). Throws PlacesError.
void savePlaces(const std::filesystem::path& file, const PlacesFile& places);

// The places named by patterns, in the order of the patterns then of the file, each once: a
// name, "*" for every place, or "prefix*" for the names starting with prefix. Throws PlacesError
// for a pattern that names no place.
[[nodiscard]] std::vector<Place> matchPlaces(const PlacesFile& file,
                                             std::span<const std::string> patterns);

} // namespace terrain::dev
