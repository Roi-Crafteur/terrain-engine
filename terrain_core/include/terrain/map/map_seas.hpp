#pragma once

#include "terrain/map/map_layers.hpp"

#include <memory>

// The layer of the floors of the seas on the dev map (agent 51, seas.hpp): over the sea, the axes
// of the mid-ocean ridges, the trenches, the fracture zones, the oceanic plateaus and the
// seamounts, at every zoom (read from the geography: Seas::mapAt). Nothing without the seas.

namespace terrain {

class Hydrology;

inline constexpr const char* kMapSeasLayer = "seas";

[[nodiscard]] MapLayerFn seasLayer(std::shared_ptr<const Hydrology> hydrology);

// The colors of the layer (opaque).
inline constexpr MapColor kMapRidgeColor{220, 60, 60, 255};
inline constexpr MapColor kMapTrenchColor{80, 20, 120, 255};
inline constexpr MapColor kMapFractureColor{235, 150, 60, 255};
inline constexpr MapColor kMapPlateauColor{70, 190, 170, 255};
inline constexpr MapColor kMapSeamountColor{250, 215, 70, 255};

} // namespace terrain
