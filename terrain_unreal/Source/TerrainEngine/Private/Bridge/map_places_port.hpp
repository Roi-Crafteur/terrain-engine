#pragma once

// Pure C++ layer of the Unreal plugin (no Unreal header): compiled by Unreal with the plugin and
// by CMake for its tests (tests/unreal_map_*_test.cpp).
//
// The saved places as the dev map sees them (agent 35): a list to show and a way to add one. The
// places are those of the tools of agent 36 (terrain/dev/places_file.hpp, data/dev/places.json):
// map_places_file.hpp plugs the file in, MemoryPlaces keeps them in memory (the tests).

#include "terrain/dev/places_file.hpp"

#include <string>
#include <utility>
#include <vector>

namespace terrain::unreal {

// Where the dev map finds the places and adds one.
class MapPlaceSource {
public:
    virtual ~MapPlaceSource() = default;
    // The places, in their order (empty when they cannot be read: error() says why).
    [[nodiscard]] virtual std::vector<dev::Place> places() const = 0;
    // Adds a place, or replaces the known values of the place of the same name
    // (dev::PlacesFile::put); false with the reason in error().
    virtual bool add(const dev::Place& place) = 0;
    // Why the last read or write failed, empty if it did not.
    [[nodiscard]] virtual std::string error() const = 0;
};

// Places kept in memory, added as the file adds them (dev::PlacesFile::put).
class MemoryPlaces final : public MapPlaceSource {
public:
    MemoryPlaces() = default;
    explicit MemoryPlaces(std::vector<dev::Place> places) { file_.places = std::move(places); }

    [[nodiscard]] std::vector<dev::Place> places() const override { return file_.places; }
    bool add(const dev::Place& place) override {
        try {
            file_.put(place);
        } catch (const dev::PlacesError& e) {
            error_ = e.what();
            return false;
        }
        error_.clear();
        return true;
    }
    [[nodiscard]] std::string error() const override { return error_; }

private:
    dev::PlacesFile file_;
    std::string error_;
};

} // namespace terrain::unreal
