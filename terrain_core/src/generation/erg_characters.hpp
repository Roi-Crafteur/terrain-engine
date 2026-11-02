#pragma once

// The characters of the ergs (agent 34, ergs.hpp: ErgCharacter), internal to terrain_core: each
// erg of the planet takes one, deterministically, never from its biome.
//
// An erg: a set of blocks of the field (erg_field.hpp) joined by their sides (on the torus) whose
// cover is above 0; its label the index of its first block; its area the sum of the cover of its
// blocks. The rule, ergs from the largest (the lower label first between equal areas):
// - the three largest take the three characters, in an order drawn from the seed (one of the 6
//   permutations): every character among the five largest;
// - every other takes the character whose area lags most behind its target share of the total
//   (ErgParams: 40 % realistic, 35 % big waves, 25 % giant waves), the draw of a hash of the seed
//   and its label between equal lags.
// Then every block takes the character of the nearest erg (a search from all the ergs at once,
// the lower label first): the weights of the characters are defined everywhere, one per block,
// and the blocks interpolate them (erg_field::blockAt).

#include "ergs_impl.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain::erg_characters {

struct Assignment {
    std::vector<float> waves;  // per block: 1 where the big waves, else 0
    std::vector<float> giants; // per block: 1 where the giant waves, else 0
    std::vector<ErgSummary> ergs; // from the largest
};

// cover: per block, how much of it the ergs cover, [0, 1].
[[nodiscard]] Assignment assign(const erg_field::Field& field, const std::vector<float>& cover,
                                std::uint64_t seed, const std::array<double, 3>& targets);

} // namespace terrain::erg_characters
