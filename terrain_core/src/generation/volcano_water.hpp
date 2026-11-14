#pragma once

// Internal to the volcanoes: their water and their springs (agent 45). The basins of their
// craters, calderas and maars: the lowest point of the rim around each, found once from the relief
// of the volcano, and on every azimuth the radius within which its inner wall stays under that
// height; the single level of the lake a basin holds under the humidity of its climate. The hot
// springs, geysers, mud pots and fumaroles of the active volcanoes: their terraces of sinter,
// their pools and pots. Deterministic: functions of the volcano and of the relief alone.

#include "volcano_relief.hpp"

#include <functional>

namespace terrain::volcano_water {

// The basins of site into detail (its shape, base, cones and flows set; its springs not yet):
// ground, the relief without the volcanoes.
void addBasins(const VolcanoSite& site, volcano_relief::Detail& detail,
               const volcano_relief::GroundAt& ground);
// Its geothermal zone and springs (an active volcano), after its basins.
void addSprings(const VolcanoSite& site, volcano_relief::Detail& detail,
                const volcano_relief::GroundAt& ground);

// The level of the lake of basin under the humidity of its climate (PlanetLevel::humidityAt at
// its center): NaN for none; salt: whether the lake is salty (a dry climate).
[[nodiscard]] double lakeLevel(const volcano_relief::Basin& basin, double humidity, bool& salt);
// Whether (dx, dz) from the center of the volcano lies within the inner wall of basin.
[[nodiscard]] bool within(const volcano_relief::Basin& basin, double dx, double dz);
// Whether a valley cuts the rim of basin, which then holds no lake (the river drains it):
// carved(dx, dz), whether the rivers carve the point (dx, dz) from the center of the volcano.
[[nodiscard]] bool drained(const volcano_relief::Basin& basin,
                           const std::function<bool(double, double)>& carved);

// The terraces, pools and pots of the springs of detail at (dx, dz) into point. footprint: the
// side of the square a sample stands for (0 for a point): they give way to the ground around
// where it is too coarse for them.
void applySprings(const volcano_relief::Detail& detail, double dx, double dz, double footprint,
                  volcano_relief::Point& point);
// The spring whose pool lies at (dx, dz) at that footprint, or nullptr.
[[nodiscard]] const volcano_relief::Spring* poolAt(const volcano_relief::Detail& detail, double dx,
                                                   double dz, double footprint);

} // namespace terrain::volcano_water
