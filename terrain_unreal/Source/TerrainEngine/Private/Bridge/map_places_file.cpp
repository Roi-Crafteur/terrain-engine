#include "map_places_file.hpp"

#include <system_error>
#include <utility>

namespace terrain::unreal {

PlacesFileSource::PlacesFileSource(std::filesystem::path file) : file_(std::move(file)) {}

std::vector<dev::Place> PlacesFileSource::places() const {
    try {
        std::vector<dev::Place> out = dev::loadPlaces(file_).places;
        error_.clear();
        return out;
    } catch (const dev::PlacesError& e) {
        error_ = e.what();
        return {};
    }
}

bool PlacesFileSource::add(const dev::Place& place) {
    try {
        std::error_code missing;
        dev::PlacesFile file =
            std::filesystem::exists(file_, missing) ? dev::loadPlaces(file_) : dev::PlacesFile{};
        file.put(place);
        dev::savePlaces(file_, file);
    } catch (const dev::PlacesError& e) {
        error_ = e.what();
        return false;
    }
    error_.clear();
    return true;
}

} // namespace terrain::unreal
