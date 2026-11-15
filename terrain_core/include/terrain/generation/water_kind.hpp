#pragma once

#include <cstddef>
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

inline constexpr int kWaterKindCount = 5;

// The water level of an output where no water stands (HorizonCell::waterLevel,
// TileData::waterLevels): far below any ground. HeightSample::waterLevel has no such value: on dry
// land it holds the height of the relief.
inline constexpr float kNoWaterLevel = -1.0e9f;

// The points of the edge of the lip of a fall (RiverFall::edgeX, TileFall::edgeX).
inline constexpr std::size_t kFallEdge = 9;

} // namespace terrain
