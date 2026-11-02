#pragma once

#include "terrain/generation/ergs.hpp"
#include "terrain/map/map_layers.hpp"

#include <memory>

// The layer of the seas of dunes on the dev map (ergs.hpp): where an erg lies, tinted by the kind
// of dunes that weighs most there, more opaque where the erg is full. Nothing without ergs.
namespace terrain {

class Hydrology;

inline constexpr const char* kMapErgsLayer = "ergs";

// The layer of the ergs of a hydrology (none if it has none). It reads the intensity of the relief
// of the hydrology at each pixel (Ergs::at).
[[nodiscard]] MapLayerFn ergsLayer(std::shared_ptr<const Hydrology> hydrology);

// The color of a kind of dunes (opaque).
[[nodiscard]] MapColor mapDuneColor(DuneKind kind);

} // namespace terrain
