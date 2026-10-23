#pragma once

#include <cstdint>

namespace terrain {

// Water over a column of the surface.
enum class WaterKind : std::uint8_t {
    None,  // dry land, even below kSeaLevel (a dry depression)
    Ocean, // under the sea: where the geography puts it (the ground below kSeaLevel without it)
    River, // a river: in its channel (the hydrology, phase 11B) or a river cell of a region
    Lake,  // a lake of fresh water, at its level
    SaltLake, // a lake without outlet in a dry climate, at the level its evaporation keeps
};

} // namespace terrain
