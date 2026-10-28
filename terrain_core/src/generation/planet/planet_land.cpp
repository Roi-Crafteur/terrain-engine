#include "terrain/generation/planet/planet_land.hpp"

#include "terrain/generation/planet/planet_relief.hpp"

namespace terrain::planet {

LandIndex::LandIndex(const std::vector<std::uint8_t>& ocean)
    : bits_((ocean.size() + 63U) / 64U, 0), before_(bits_.size(), 0), cells_(ocean.size()) {
    for (std::size_t c = 0; c < ocean.size(); ++c) {
        if (ocean[c] == kLand) {
            bits_[c >> 6] |= std::uint64_t{1} << (c & 63U);
        }
    }
    for (std::size_t word = 0; word < bits_.size(); ++word) {
        before_[word] = static_cast<std::uint32_t>(land_);
        land_ += static_cast<std::size_t>(std::popcount(bits_[word]));
    }
}

} // namespace terrain::planet
