#pragma once

// Pure C++ layer of the Unreal plugin (no Unreal header): compiled by Unreal with the plugin and
// by CMake for its tests (tests/unreal_map_*_test.cpp).
//
// The saved places as the dev map sees them (agent 35): a list to show and a way to add one. The
// file of the places (data/dev/places.json) is read and written by the tools of agent 36
// (terrain/dev/places_file.hpp); it plugs in here, the map never reads the file itself.

#include <string>
#include <utility>
#include <vector>

namespace terrain::unreal {

// How the eye of a view is given (eyeMode of the common format): an altitude of terrain_core, or
// a height above the ground or the water under it.
enum class MapEyeMode { Altitude, Ground };

// A view of the world: the fields of a place of the common format.
struct MapPlace {
    std::string name;
    double x = 0.0;       // meters of the core, x east
    double z = 0.0;       // z south
    double heading = 0.0; // degrees clockwise from the north (-z), in [0, 360)
    double pitch = 0.0;   // degrees up, in [-90, 90]
    double eye = 1.8;     // meters: an altitude, or a height above the ground or the water
    MapEyeMode eyeMode = MapEyeMode::Ground;
    bool planet = false;  // the view needs the planet file (rivers, falls, lakes)
    std::string note;

    friend bool operator==(const MapPlace&, const MapPlace&) = default;
};

// Where the dev map finds the places and adds one.
class MapPlaceSource {
public:
    virtual ~MapPlaceSource() = default;
    // The places, in their order (empty when they cannot be read: error() says why).
    [[nodiscard]] virtual std::vector<MapPlace> places() const = 0;
    // Adds a place, or replaces the place of the same name; false with the reason in error().
    virtual bool add(const MapPlace& place) = 0;
    // Why the last read or write failed, empty if it did not.
    [[nodiscard]] virtual std::string error() const = 0;
};

// Places kept in memory: the tests, and the map before a file is plugged in.
class MemoryPlaces final : public MapPlaceSource {
public:
    MemoryPlaces() = default;
    explicit MemoryPlaces(std::vector<MapPlace> places) : places_(std::move(places)) {}

    [[nodiscard]] std::vector<MapPlace> places() const override { return places_; }
    bool add(const MapPlace& place) override {
        if (place.name.empty()) {
            error_ = "a place needs a name";
            return false;
        }
        error_.clear();
        for (MapPlace& p : places_) {
            if (p.name == place.name) {
                p = place;
                return true;
            }
        }
        places_.push_back(place);
        return true;
    }
    [[nodiscard]] std::string error() const override { return error_; }

private:
    std::vector<MapPlace> places_;
    std::string error_;
};

} // namespace terrain::unreal
