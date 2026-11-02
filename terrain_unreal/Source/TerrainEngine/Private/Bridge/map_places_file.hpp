#pragma once

// Pure C++ layer of the Unreal plugin (no Unreal header): compiled by Unreal with the plugin and
// by CMake for its tests (tests/unreal_map_places_file_test.cpp).
//
// The places file of the tools of agent 36 (data/dev/places.json, or Terrain.PlacesFile) as the
// places of the dev map (agent 35): read and written by terrain/dev/places_file.hpp, never by the
// map itself.

#include "map_places_port.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace terrain::unreal {

class PlacesFileSource final : public MapPlaceSource {
public:
    explicit PlacesFileSource(std::filesystem::path file);

    // dev::loadPlaces of the file, read again at each call (Terrain.Save may have written it).
    [[nodiscard]] std::vector<dev::Place> places() const override;
    // The file read (a missing one is an empty list), the place put (dev::PlacesFile::put: the
    // members another tool wrote stay), the file saved whole (dev::savePlaces).
    bool add(const dev::Place& place) override;
    [[nodiscard]] std::string error() const override { return error_; }

    [[nodiscard]] const std::filesystem::path& file() const { return file_; }

private:
    std::filesystem::path file_;
    mutable std::string error_;
};

} // namespace terrain::unreal
