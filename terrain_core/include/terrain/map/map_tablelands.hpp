#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/map/map_layers.hpp"

#include <memory>

// The layers of the provinces of the plateaus and of the karst on the dev map (agent 46,
// tablelands.hpp). Nothing where the world has none.
namespace terrain {

class PlanetLevel;

// Where a province lies, tinted by its kind: the mesas of the dry lands (orange), the plateaus of
// the humid lands (violet), the karst (yellow), the towers of the hot and humid karst (magenta);
// more opaque where the province is full.
inline constexpr const char* kMapTablelandsLayer = "tablelands";
[[nodiscard]] MapLayerFn tablelandsLayer(const Heightfield& field);

// Where the pre-climate that places them (pre_climate.hpp) errs most against the climate of the
// planet level: karst on a land the planet level makes dry (red), mesas of the dry lands on a land
// it makes humid (blue); nothing without a planet level.
inline constexpr const char* kMapTablelandErrorsLayer = "tableland_errors";
[[nodiscard]] MapLayerFn tablelandErrorsLayer(const Heightfield& field,
                                              std::shared_ptr<const PlanetLevel> planet);

// The thresholds of the errors, on the humidity of the planet level (PlanetLevel::humidityAt):
// karst below kKarstDryHumidity, mesas of the dry lands above kMesaHumidHumidity.
inline constexpr float kKarstDryHumidity = 0.38f;
inline constexpr float kMesaHumidHumidity = 0.45f;

} // namespace terrain
