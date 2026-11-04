#pragma once

#include "terrain/generation/volcanoes.hpp"
#include "terrain/map/map_frame.hpp"
#include "terrain/map/map_layers.hpp"

#include <span>

// The layer of the volcanoes on the dev map (agent 45, volcanoes.hpp): the ground their relief
// made, tinted by the form of their volcano, more opaque where they made more of it
// (MapSample::volcanic). On the coarse maps of the planet, a volcano takes at least a pixel: every
// volcano of the planet shows on its whole map.
namespace terrain {

inline constexpr const char* kMapVolcanoesLayer = "volcanoes";

// The color of a form of volcano (opaque).
[[nodiscard]] MapColor mapVolcanoColor(VolcanoForm form);
// The layer: it reads the sample of the pixel only (MapSample::volcanic and volcano).
[[nodiscard]] MapColor volcanoesLayer(const MapPoint& point);

// Marks the volcanoes on the samples of a frame (its pixels and their border of one pixel, as the
// map sources fill them): the form of the volcano whose reach holds each pixel, nearest relative
// to its reach (MapSample::volcano), and the pixel of its center as its ground (volcanic 255) so
// that every volcano shows at any spacing; with cells (the samples of the cells of the planet
// level, which hold no relief of theirs), the whole foot of each edifice too. On up to threads
// threads, the same bytes whatever their number.
void markVolcanoes(const Volcanoes& volcanoes, const MapFrame& frame, std::span<MapSample> out,
                   bool cells, int threads);

} // namespace terrain
