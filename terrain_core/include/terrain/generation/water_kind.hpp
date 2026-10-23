#pragma once

#include <cstdint>

namespace terrain {

// Water over a column of the surface.
enum class WaterKind : std::uint8_t {
    None,  // dry land, even below kSeaLevel (a dry depression)
    Ocean, // under the sea: where the geography puts it (the ground below kSeaLevel without it)
    River, // river cell of the drainage network: its water is at the ground height
};

} // namespace terrain
