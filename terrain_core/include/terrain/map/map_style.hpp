#pragma once

#include "terrain/map/map_layers.hpp"

#include <string>
#include <vector>

// The layers the dev map is born with (map_layers.hpp): the shaded relief, the biomes, the water
// and the contours. Their colors follow the PNG of the relief (io/relief_png.cpp) and of the
// biomes (io/ecology_png.hpp).
namespace terrain {

inline constexpr const char* kMapReliefLayer = "relief";
inline constexpr const char* kMapBiomesLayer = "biomes";
inline constexpr const char* kMapWaterLayer = "water";
inline constexpr const char* kMapContoursLayer = "contours";

// relief, biomes, water and contours, in this order.
[[nodiscard]] MapLayers defaultMapLayers();
// The layers drawn at first: relief and water.
[[nodiscard]] std::vector<std::string> defaultMapLayerNames();

// The hypsometric tints, opaque: the sea floor in blues, the dry land below the sea level from
// clay to salt, the land from lowland greens to the snow; lit by mapShade.
[[nodiscard]] MapColor reliefLayer(const MapPoint& p);
// The colors of the biomes (kBiomeRgb), lit by mapShade; nothing over the ocean.
[[nodiscard]] MapColor biomeLayer(const MapPoint& p);
// The sea, translucent over its floor; the lakes and salt lakes; the rivers, darker the larger
// their discharge, over the part of the pixel their course covers; the marks of the falls (a red
// ring around a white center).
[[nodiscard]] MapColor waterLayer(const MapPoint& p);
// The contours of the ground out of the sea and the lakes, every mapContourInterval meters,
// darker every 5 of them.
[[nodiscard]] MapColor contourLayer(const MapPoint& p);

// The light of the relief at a pixel, 1 on flat ground: Lambert lit from the north-west 45
// degrees high, the slopes of the neighbor pixels exaggerated by mapReliefExaggeration, in
// [0.2, 1.35].
[[nodiscard]] float mapShade(const MapPoint& p);
// sqrt(metersPerPixel / 10) within [1, 30]: coarser maps flatten the slopes, this keeps them.
[[nodiscard]] double mapReliefExaggeration(double metersPerPixel);
// The contour spacing at a spacing of the pixels: the first of 5, 10, 20, 50, 100, 200, 500,
// 1000 and 2000 m at least 6 pixels.
[[nodiscard]] double mapContourInterval(double metersPerPixel);
// The color of a river of this discharge (m^3/s), from light blue at 10 m^3/s to deep blue at
// 100,000 m^3/s.
[[nodiscard]] MapColor mapRiverColor(float discharge);

} // namespace terrain
