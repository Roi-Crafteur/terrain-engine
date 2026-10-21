#pragma once

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/io/png.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace terrain {

// Control images of the planet level, the whole world in one picture: north (row 0) at the top,
// east to the right. One pixel covers scale x scale cells (the PNG files are not compressed:
// the full grid of the default world would weigh 94 MB per image).

// The smallest whole number of cells per pixel that keeps the image within maxWidth pixels.
[[nodiscard]] int planetImageScale(const PlanetGrid& grid, int maxWidth);

// Hypsometric colors of the mean altitude of each pixel, shaded by the slope lit from the
// north-west; the sea in blues by depth.
[[nodiscard]] Image renderPlanetRelief(const PlanetLevel& level, int scale);
// Precipitation (mm/yr), from the tan of the deserts to the purple of the wettest coasts;
// darker over the sea.
[[nodiscard]] Image renderPlanetPrecipitation(const PlanetLevel& level, int scale);
// Mean annual temperature, from purple (-40 degrees) through white (0) to red (+35); frozen
// land in pale blue; darker over the sea.
[[nodiscard]] Image renderPlanetTemperature(const PlanetLevel& level, int scale);
// Drainage basins (PlanetLevel::basins), one color each, the sea in dark blue.
[[nodiscard]] Image renderPlanetBasins(const PlanetLevel& level,
                                       const std::vector<std::int32_t>& basins, int scale);
// The rivers over a gray relief, thicker and brighter with their discharge.
[[nodiscard]] Image renderPlanetRivers(const PlanetLevel& level, int scale);
// The lakes over a gray relief: overflowing in blue, terminal in teal, frozen in pale blue,
// playas as salt-white dots; the rivers thin.
[[nodiscard]] Image renderPlanetLakes(const PlanetLevel& level, int scale);

// Writes planet_relief.png, planet_precipitation.png, planet_temperature.png,
// planet_basins.png, planet_rivers.png and planet_lakes.png into directory (created if needed),
// at most maxWidth pixels wide. Returns false if a file cannot be written.
[[nodiscard]] bool exportPlanetPngs(const PlanetLevel& level,
                                    const std::filesystem::path& directory, int maxWidth = 4096);

} // namespace terrain
